#ifndef VECTORSEARCH_HPP
#define VECTORSEARCH_HPP

#include <array>
#include <vector>
#include <string>

struct Reference {
    std::array<float, 14> vector;
    bool fraud;
};

struct Neighbor {
    float distance;
    bool fraud;
};

std::array<bool, 5> kNearestNeighbor(const std::array<float, 14>& queryVector);
const std::vector<Reference>& getReferences();
std::vector<Reference> loadReferences(const std::string& path);

#endif
