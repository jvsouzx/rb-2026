#include "vectorSearch.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <iostream>
#include <cmath>
#include <utility>
#include <immintrin.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "quantize.hpp"

namespace {
    constexpr std::uint32_t ReferencesMagic = 0x32464252;
    constexpr int VectorDimensions = 14;
    constexpr std::size_t HeaderSize = sizeof(std::uint32_t) * 2;
    constexpr int CoarseBucketShift = 5;
    constexpr int CoarseBucketCount = 8;
    constexpr int FlagBits = 3;
    constexpr int TotalBucketBits = (3 * 6) + FlagBits;
    constexpr int BucketCount = 1 << TotalBucketBits;
    thread_local SearchStats lastSearchStats;

    struct BucketKey {
        int amount;
        int amountVsAvg;
        int hourOfDay;
        int kmFromHome;
        int txCount24h;
        int mccRisk;
        int flags;
    };

    int readIntEnv(const char* name, int fallback) {
        const char* value = std::getenv(name);
        if (value == nullptr) {
            return fallback;
        }

        try {
            return std::stoi(value);
        } catch (...) {
            return fallback;
        }
    }

    int bucketMaxRadius() {
        static const int value = std::clamp(readIntEnv("RB_BUCKET_MAX_RADIUS", 1), 0, CoarseBucketCount - 1);
        return value;
    }

    std::size_t bucketMinCandidates() {
        static const int value = std::max(5, readIntEnv("RB_BUCKET_MIN_CANDIDATES", 5000));
        return static_cast<std::size_t>(value);
    }

    bool validBucketValue(int value) {
        return value >= 0 && value < CoarseBucketCount;
    }

    BucketKey makeBucketKey(const std::uint8_t* vector) {
        int flags = 0;
        if (vector[9] > 127) {
            flags |= 1 << 0;
        }
        if (vector[10] > 127) {
            flags |= 1 << 1;
        }
        if (vector[11] > 127) {
            flags |= 1 << 2;
        }

        return BucketKey{
            static_cast<int>(vector[0] >> CoarseBucketShift),
            static_cast<int>(vector[2] >> CoarseBucketShift),
            static_cast<int>(vector[3] >> CoarseBucketShift),
            static_cast<int>(vector[7] >> CoarseBucketShift),
            static_cast<int>(vector[8] >> CoarseBucketShift),
            static_cast<int>(vector[12] >> CoarseBucketShift),
            flags
        };
    }

    BucketKey makeBucketKey(const std::array<std::uint8_t, 14>& vector) {
        return makeBucketKey(vector.data());
    }

    int bucketIndex(const BucketKey& key) {
        return key.amount
            | (key.amountVsAvg << 3)
            | (key.hourOfDay << 6)
            | (key.kmFromHome << 9)
            | (key.txCount24h << 12)
            | (key.mccRisk << 15)
            | (key.flags << 18);
    }

    int horizontalSum8x32(__m256i values) {
        __m128i low = _mm256_castsi256_si128(values);
        __m128i high = _mm256_extracti128_si256(values, 1);
        __m128i sum = _mm_add_epi32(low, high);
        sum = _mm_hadd_epi32(sum, sum);
        sum = _mm_hadd_epi32(sum, sum);
        return _mm_cvtsi128_si32(sum);
    }

    __m128i paddedQueryBytes(const std::array<std::uint8_t, 14>& queryVector) {
        alignas(16) std::uint8_t padded[16] = {};
        std::memcpy(padded, queryVector.data(), queryVector.size());
        return _mm_load_si128(reinterpret_cast<const __m128i*>(padded));
    }

    int euclideanDistanceAvx2(__m128i queryBytes, const std::uint8_t* referenceVector) {
        const __m128i mask = _mm_set_epi8(
            0, 0,
            static_cast<char>(0xff), static_cast<char>(0xff),
            static_cast<char>(0xff), static_cast<char>(0xff),
            static_cast<char>(0xff), static_cast<char>(0xff),
            static_cast<char>(0xff), static_cast<char>(0xff),
            static_cast<char>(0xff), static_cast<char>(0xff),
            static_cast<char>(0xff), static_cast<char>(0xff),
            static_cast<char>(0xff), static_cast<char>(0xff));

        __m128i refBytes = _mm_loadu_si128(reinterpret_cast<const __m128i*>(referenceVector));
        refBytes = _mm_and_si128(refBytes, mask);

        __m256i refWords = _mm256_cvtepu8_epi16(refBytes);
        __m256i queryWords = _mm256_cvtepu8_epi16(queryBytes);
        __m256i diff = _mm256_sub_epi16(refWords, queryWords);
        __m256i squares = _mm256_madd_epi16(diff, diff);

        return horizontalSum8x32(squares);
    }

    int findWorstNeighborIndex(const std::array<Neighbor, 5>& neighbors) {
        int worstIndex = 0;
        for (int i = 1; i < 5; ++i) {
            if (neighbors[i].distance > neighbors[worstIndex].distance) {
                worstIndex = i;
            }
        }

        return worstIndex;
    }

    void updateTop5(std::array<Neighbor, 5>& nearest, int& nearestSize, int& worstIndex, Neighbor candidate) {
        if (nearestSize < 5) {
            nearest[nearestSize] = candidate;
            nearestSize++;
            if (nearestSize == 5) {
                worstIndex = findWorstNeighborIndex(nearest);
            }
            return;
        }

        if (candidate.distance < nearest[worstIndex].distance) {
            nearest[worstIndex] = candidate;
            worstIndex = findWorstNeighborIndex(nearest);
        }
    }

    void buildBucketIndex(ReferenceStore& store) {
        store.bucketOffsets.assign(BucketCount + 1, 0);

        for (std::uint32_t i = 0; i < store.count; ++i) {
            const std::uint8_t* vector = store.vectors + static_cast<std::size_t>(i) * VectorDimensions;
            int bucket = bucketIndex(makeBucketKey(vector));
            store.bucketOffsets[bucket + 1]++;
        }

        for (int i = 1; i <= BucketCount; ++i) {
            store.bucketOffsets[i] += store.bucketOffsets[i - 1];
        }

        store.bucketIds.resize(store.count);
        std::vector<std::uint32_t> writePositions = store.bucketOffsets;

        for (std::uint32_t i = 0; i < store.count; ++i) {
            const std::uint8_t* vector = store.vectors + static_cast<std::size_t>(i) * VectorDimensions;
            int bucket = bucketIndex(makeBucketKey(vector));
            std::uint32_t position = writePositions[bucket]++;
            store.bucketIds[position] = i;
        }

        std::cout << "Indice de buckets criado com " << BucketCount << " buckets\n";
    }
}

ReferenceStore::~ReferenceStore() {
    if (mappedData != nullptr && mappedSize > 0) {
        munmap(const_cast<std::uint8_t*>(mappedData), mappedSize);
    }
}

ReferenceStore::ReferenceStore(ReferenceStore&& other) noexcept
    : count(other.count),
      mappedSize(other.mappedSize),
      mappedData(other.mappedData),
      vectors(other.vectors),
      labels(other.labels),
      bucketOffsets(std::move(other.bucketOffsets)),
      bucketIds(std::move(other.bucketIds)) {
    other.count = 0;
    other.mappedSize = 0;
    other.mappedData = nullptr;
    other.vectors = nullptr;
    other.labels = nullptr;
}

ReferenceStore& ReferenceStore::operator=(ReferenceStore&& other) noexcept {
    if (this != &other) {
        if (mappedData != nullptr && mappedSize > 0) {
            munmap(const_cast<std::uint8_t*>(mappedData), mappedSize);
        }

        count = other.count;
        mappedSize = other.mappedSize;
        mappedData = other.mappedData;
        vectors = other.vectors;
        labels = other.labels;
        bucketOffsets = std::move(other.bucketOffsets);
        bucketIds = std::move(other.bucketIds);

        other.count = 0;
        other.mappedSize = 0;
        other.mappedData = nullptr;
        other.vectors = nullptr;
        other.labels = nullptr;
    }

    return *this;
}

ReferenceStore loadBinaryReferences(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        throw std::runtime_error("Erro ao abrir " + path);
    }

    struct stat fileStat {};
    if (fstat(fd, &fileStat) == -1) {
        close(fd);
        throw std::runtime_error("Erro ao obter tamanho de " + path);
    }

    if (fileStat.st_size < static_cast<off_t>(HeaderSize)) {
        close(fd);
        throw std::runtime_error("Arquivo de referencias binario muito pequeno: " + path);
    }

    std::size_t mappedSize = static_cast<std::size_t>(fileStat.st_size);
    void* mapping = mmap(nullptr, mappedSize, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (mapping == MAP_FAILED) {
        throw std::runtime_error("Erro ao mapear " + path);
    }

    const auto* data = static_cast<const std::uint8_t*>(mapping);

    std::uint32_t magic = 0;
    std::uint32_t count = 0;
    std::memcpy(&magic, data, sizeof(magic));
    std::memcpy(&count, data + sizeof(magic), sizeof(count));

    if (magic != ReferencesMagic) {
        munmap(mapping, mappedSize);
        throw std::runtime_error("Arquivo de referencias binario invalido: " + path);
    }

    std::size_t vectorsSize = static_cast<std::size_t>(count) * VectorDimensions;
    std::size_t labelsSize = count;
    std::size_t expectedSize = HeaderSize + vectorsSize + labelsSize;

    if (mappedSize != expectedSize) {
        munmap(mapping, mappedSize);
        throw std::runtime_error("Tamanho inesperado do arquivo de referencias: " + path);
    }

    ReferenceStore store;
    store.count = count;
    store.mappedSize = mappedSize;
    store.mappedData = data;
    store.vectors = data + HeaderSize;
    store.labels = store.vectors + vectorsSize;
    buildBucketIndex(store);

    std::cout << "Carregadas " << store.count << " referencias binarias\n";
    return store;
}

const ReferenceStore& getReferences(){
    static const ReferenceStore references = loadBinaryReferences("resources/references.bin");
    return references;
}

int euclideanDistance(const std::array<uint8_t, 14>& queryVector, const uint8_t* referenceVector) {
    int distance = 0;
    for (int i = 0; i < VectorDimensions; i++){
        int diff = static_cast<int>(referenceVector[i]) - static_cast<int>(queryVector[i]);
        distance += diff * diff;
    }
    return distance;
}

SearchStats getLastSearchStats() {
    return lastSearchStats;
}

DistanceValidationResult validateDistanceImplementations(const std::array<std::uint8_t, 14>& queryVector, std::size_t sampleCount) {
    const ReferenceStore& refs = getReferences();
    __m128i queryBytes = paddedQueryBytes(queryVector);
    std::size_t checked = std::min<std::size_t>(sampleCount, refs.count);

    DistanceValidationResult result;
    result.checked = checked;

    for (std::size_t i = 0; i < checked; ++i) {
        const std::uint8_t* vector = refs.vectors + i * VectorDimensions;
        int scalarDistance = euclideanDistance(queryVector, vector);
        int avx2Distance = euclideanDistanceAvx2(queryBytes, vector);

        if (scalarDistance != avx2Distance) {
            result.mismatches++;
            if (result.mismatches == 1) {
                result.firstMismatchIndex = static_cast<std::uint32_t>(i);
                result.firstScalarDistance = scalarDistance;
                result.firstAvx2Distance = avx2Distance;
            }
        }
    }

    return result;
}

std::array<bool, 5> kNearestNeighbor(const std::array<uint8_t, 14>& queryVector){
    const ReferenceStore& refs = getReferences();
    std::array<Neighbor, 5> nearest{};
    int nearestSize = 0;
    int worstIndex = 0;
    __m128i queryBytes = paddedQueryBytes(queryVector);
    BucketKey queryBucket = makeBucketKey(queryVector);
    SearchStats stats;

    auto scanReference = [&](std::uint32_t id) {
        const uint8_t* vector = refs.vectors + static_cast<std::size_t>(id) * VectorDimensions;
        int distance = euclideanDistanceAvx2(queryBytes, vector);
        updateTop5(nearest, nearestSize, worstIndex, Neighbor{distance, refs.labels[id] == 1});
        stats.candidatesScanned++;
    };

    auto scanBucket = [&](int bucket) {
        std::uint32_t begin = refs.bucketOffsets[bucket];
        std::uint32_t end = refs.bucketOffsets[bucket + 1];
        for (std::uint32_t pos = begin; pos < end; ++pos) {
            scanReference(refs.bucketIds[pos]);
        }
    };

    int maxRadius = bucketMaxRadius();
    for (int radius = 0; radius <= maxRadius; ++radius) {
        for (int amountDelta = -radius; amountDelta <= radius; ++amountDelta) {
            for (int amountVsAvgDelta = -radius; amountVsAvgDelta <= radius; ++amountVsAvgDelta) {
                for (int hourDelta = -radius; hourDelta <= radius; ++hourDelta) {
                    for (int kmDelta = -radius; kmDelta <= radius; ++kmDelta) {
                        for (int txDelta = -radius; txDelta <= radius; ++txDelta) {
                            for (int mccDelta = -radius; mccDelta <= radius; ++mccDelta) {
                                if (std::max({std::abs(amountDelta), std::abs(amountVsAvgDelta), std::abs(hourDelta), std::abs(kmDelta), std::abs(txDelta), std::abs(mccDelta)}) != radius) {
                                    continue;
                                }

                                BucketKey candidateKey{
                                    queryBucket.amount + amountDelta,
                                    queryBucket.amountVsAvg + amountVsAvgDelta,
                                    queryBucket.hourOfDay + hourDelta,
                                    queryBucket.kmFromHome + kmDelta,
                                    queryBucket.txCount24h + txDelta,
                                    queryBucket.mccRisk + mccDelta,
                                    queryBucket.flags
                                };

                                if (!validBucketValue(candidateKey.amount)
                                    || !validBucketValue(candidateKey.amountVsAvg)
                                    || !validBucketValue(candidateKey.hourOfDay)
                                    || !validBucketValue(candidateKey.kmFromHome)
                                    || !validBucketValue(candidateKey.txCount24h)
                                    || !validBucketValue(candidateKey.mccRisk)) {
                                    continue;
                                }

                                scanBucket(bucketIndex(candidateKey));
                            }
                        }
                    }
                }
            }
        }

        stats.radiusUsed = radius;
        if (nearestSize == 5 && stats.candidatesScanned >= bucketMinCandidates()) {
            break;
        }
    }

    if (nearestSize < 5) {
        stats.bruteForceFallback = true;
        for (std::uint32_t i = 0; i < refs.count; ++i) {
            scanReference(i);
        }
    }

    lastSearchStats = stats;

    std::array<bool, 5> result{};
    for (int i = 0; i < nearestSize; ++i){
        result[i] = nearest[i].fraud;
    }
    
    return result;
}

FraudScoreResult transactionIsApproved(const std::array<float, 14>& queryVector){
    std::array<uint8_t, 14> quantQuery = quantizeVector(queryVector);
    std::array<bool, 5> nearest = kNearestNeighbor(quantQuery);
    int fraudCount = 0;

    for (bool isFraud : nearest) {
        if (isFraud) {
            fraudCount++;
        }
    }

    float fraudScore = fraudCount / 5.0f;
    bool approved = fraudScore < 0.6f;

    return FraudScoreResult{approved, fraudScore};
}
