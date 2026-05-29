#include "vectorSearch.hpp"
#include <cstdint>
#include <stdexcept>
#include <fstream>
#include <iostream>
#include <cmath>
#include "quantize.hpp"

namespace {
    constexpr std::uint32_t ReferencesMagic = 0x32464252;
    constexpr int VectorDimensions = 14;
}

ReferenceStore loadBinaryReferences(const std::string& path){
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Erro ao abrir " + path);
    }

    std::uint32_t magic = 0;
    std::uint32_t count = 0;

    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&count), sizeof(count));

    if (!file || magic != ReferencesMagic) {
        throw std::runtime_error("Arquivo de referencias binario invalido: " + path);
    }

    ReferenceStore store;
    store.count = count;
    store.vectors.resize(static_cast<std::size_t>(count) * VectorDimensions);
    store.labels.resize(count);

    file.read(reinterpret_cast<char*>(store.vectors.data()),
              sizeof(std::uint8_t) * store.vectors.size());
    if (!file) {
        throw std::runtime_error("Erro ao ler vetores binarios em " + path);
    }

    file.read(reinterpret_cast<char*>(store.labels.data()),
              sizeof(std::uint8_t) * store.labels.size());
    if (!file) {
        throw std::runtime_error("Erro ao ler labels binarios em " + path);
    }

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
        const uint8_t* vector = &refs.vectors[static_cast<std::size_t>(i) * VectorDimensions];
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
