#include "vectorSearch.hpp"
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <cmath>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "quantize.hpp"

namespace {
    constexpr std::uint32_t ReferencesMagic = 0x32464252;
    constexpr int VectorDimensions = 14;
    constexpr std::size_t HeaderSize = sizeof(std::uint32_t) * 2;
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
      labels(other.labels) {
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

std::array<bool, 5> kNearestNeighbor(const std::array<uint8_t, 14>& queryVector){
    // carregar/receber references
    // para cada Reference ref:
    //      distance = euclideanDistance(queryVector, ref.vector)
    //      se distance estiver entre as 5 menores:
    //          guardar Neighbor{distance, ref.fraud}
    // retornar os 5 menores
    const ReferenceStore& refs = getReferences();
    std::priority_queue<Neighbor, std::vector<Neighbor>, CompareNeighbor> nearest;

    for (std::uint32_t i = 0; i < refs.count; ++i){
        const uint8_t* vector = refs.vectors + static_cast<std::size_t>(i) * VectorDimensions;
        int distance = euclideanDistance(queryVector, vector);
        Neighbor candidate{distance, refs.labels[i] == 1};
        if (nearest.size() < 5) {
            nearest.push(candidate);
        } else if (candidate.distance < nearest.top().distance) {
            nearest.pop();
            nearest.push(candidate);
        }
    }

    std::array<bool, 5> result{};
    for (int i = 4; i >= 0 && !nearest.empty(); i--){
        result[i] = nearest.top().fraud;
        nearest.pop();
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
