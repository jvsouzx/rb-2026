#ifndef VECTORSEARCH_HPP
#define VECTORSEARCH_HPP

#include <array>
#include <vector>
#include <string>
#include <queue>

struct Reference {
    std::array<float, 14> vector;
    bool fraud;
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
const std::vector<Reference>& getReferences();
std::vector<Reference> loadReferences(const std::string& path);
float euclideanDistance(const std::array<float, 14>& queryVector, const std::array<float, 14>& referenceVector);

#endif
