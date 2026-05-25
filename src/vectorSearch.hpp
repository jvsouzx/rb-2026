#ifndef VECTORSEARCH_HPP
#define VECTORSEARCH_HPP

#include <array>
#include <cstdint>
#include <vector>
#include <string>
#include <queue>

struct ReferenceStore {
    std::uint32_t count;
    std::vector<float> vectors;
    std::vector<std::uint8_t> labels;
};

struct Neighbor {
    float distance;
    bool fraud;
};

struct CompareNeighbor {
    bool operator()(const Neighbor& a, const Neighbor& b) const {
        return a.distance < b.distance;
    }
};

struct FraudScoreResult {
    bool approved;
    float fraudScore;
};

std::array<bool, 5> kNearestNeighbor(const std::array<float, 14>& queryVector);
FraudScoreResult transactionIsApproved(const std::array<float, 14>& queryVector);
const ReferenceStore& getReferences();
ReferenceStore loadBinaryReferences(const std::string& path);
float euclideanDistance(const std::array<float, 14>& queryVector, const float* referenceVector);

#endif
