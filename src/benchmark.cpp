#include "quantize.hpp"
#include "vectorSearch.hpp"
#include "vectorize.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {
    std::size_t readSizeEnv(const char* name, std::size_t fallback) {
        const char* value = std::getenv(name);
        if (value == nullptr) {
            return fallback;
        }

        try {
            return static_cast<std::size_t>(std::stoull(value));
        } catch (...) {
            return fallback;
        }
    }

    double toMilliseconds(std::chrono::steady_clock::duration duration) {
        return std::chrono::duration<double, std::milli>(duration).count();
    }

    double percentile(const std::vector<double>& sortedValues, double percentile) {
        if (sortedValues.empty()) {
            return 0.0;
        }

        std::size_t index = static_cast<std::size_t>(
            std::ceil((percentile / 100.0) * sortedValues.size())) - 1;

        return sortedValues[std::min(index, sortedValues.size() - 1)];
    }
}

int main() {
    const std::size_t validationSamples = readSizeEnv("BENCH_VALIDATE_SAMPLES", 100000);
    const std::size_t requestLimit = readSizeEnv("BENCH_REQUEST_LIMIT", 50);

    std::ifstream payloadFile("resources/example-payloads.json");
    if (!payloadFile) {
        std::cerr << "Erro ao abrir resources/example-payloads.json\n";
        return 1;
    }

    json payloads = json::parse(payloadFile);
    if (!payloads.is_array() || payloads.empty()) {
        std::cerr << "resources/example-payloads.json nao contem payloads\n";
        return 1;
    }

    std::array<float, 14> firstVector = vectorizeTransaction(payloads[0].dump());
    std::array<std::uint8_t, 14> firstQuantizedVector = quantizeVector(firstVector);

    DistanceValidationResult validation = validateDistanceImplementations(firstQuantizedVector, validationSamples);
    std::cout << "distance_validation.checked=" << validation.checked << "\n";
    std::cout << "distance_validation.mismatches=" << validation.mismatches << "\n";

    if (validation.mismatches > 0) {
        std::cout << "distance_validation.first_mismatch_index=" << validation.firstMismatchIndex << "\n";
        std::cout << "distance_validation.first_scalar=" << validation.firstScalarDistance << "\n";
        std::cout << "distance_validation.first_avx2=" << validation.firstAvx2Distance << "\n";
        return 1;
    }

    std::size_t requests = std::min<std::size_t>(requestLimit, payloads.size());
    double totalMs = 0.0;
    double minMs = std::numeric_limits<double>::max();
    double maxMs = 0.0;
    int denied = 0;
    std::vector<double> durationsMs;
    std::vector<double> candidatesScanned;
    durationsMs.reserve(requests);
    candidatesScanned.reserve(requests);
    int bruteForceFallbacks = 0;
    int maxRadiusUsed = 0;

    for (std::size_t i = 0; i < requests; ++i) {
        std::array<float, 14> vector = vectorizeTransaction(payloads[i].dump());

        auto start = std::chrono::steady_clock::now();
        FraudScoreResult result = transactionIsApproved(vector);
        auto end = std::chrono::steady_clock::now();

        double elapsedMs = toMilliseconds(end - start);
        totalMs += elapsedMs;
        minMs = std::min(minMs, elapsedMs);
        maxMs = std::max(maxMs, elapsedMs);
        durationsMs.push_back(elapsedMs);

        if (!result.approved) {
            denied++;
        }

        SearchStats stats = getLastSearchStats();
        candidatesScanned.push_back(static_cast<double>(stats.candidatesScanned));
        if (stats.bruteForceFallback) {
            bruteForceFallbacks++;
        }
        maxRadiusUsed = std::max(maxRadiusUsed, stats.radiusUsed);
    }

    std::sort(durationsMs.begin(), durationsMs.end());
    std::sort(candidatesScanned.begin(), candidatesScanned.end());

    std::cout << "requests=" << requests << "\n";
    std::cout << "denied=" << denied << "\n";
    std::cout << "knn_avg_ms=" << (totalMs / requests) << "\n";
    std::cout << "knn_min_ms=" << minMs << "\n";
    std::cout << "knn_p50_ms=" << percentile(durationsMs, 50) << "\n";
    std::cout << "knn_p90_ms=" << percentile(durationsMs, 90) << "\n";
    std::cout << "knn_p95_ms=" << percentile(durationsMs, 95) << "\n";
    std::cout << "knn_p99_ms=" << percentile(durationsMs, 99) << "\n";
    std::cout << "knn_max_ms=" << maxMs << "\n";
    std::cout << "candidate_p50=" << percentile(candidatesScanned, 50) << "\n";
    std::cout << "candidate_p90=" << percentile(candidatesScanned, 90) << "\n";
    std::cout << "candidate_p99=" << percentile(candidatesScanned, 99) << "\n";
    std::cout << "max_radius_used=" << maxRadiusUsed << "\n";
    std::cout << "brute_force_fallbacks=" << bruteForceFallbacks << "\n";

    return 0;
}
