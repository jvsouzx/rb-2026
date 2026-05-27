#include "vectorSearch.hpp"
#include <cstdint>
#include <stdexcept>
#include <fstream>
#include <iostream>
#include <cmath>

namespace {
    constexpr std::uint32_t ReferencesMagic = 0x31464252;
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
              sizeof(float) * store.vectors.size());
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

float euclideanDistance(const std::array<float, 14>& queryVector, const float* referenceVector) {
    float distance = 0.0f;
    for (int i = 0; i < VectorDimensions; i++){
        float diff = (referenceVector[i] - queryVector[i]);
        distance += diff * diff;
    }
    return distance;
}

std::array<bool, 5> kNearestNeighbor(const std::array<float, 14>& queryVector){
    // carregar/receber references
    // para cada Reference ref:
    //      distance = euclideanDistance(queryVector, ref.vector)
    //      se distance estiver entre as 5 menores:
    //          guardar Neighbor{distance, ref.fraud}
    // retornar os 5 menores
    const ReferenceStore& refs = getReferences();
    std::priority_queue<Neighbor, std::vector<Neighbor>, CompareNeighbor> nearest;

    for (std::uint32_t i = 0; i < refs.count; ++i){
        const float* vector = &refs.vectors[static_cast<std::size_t>(i) * VectorDimensions];
        float distance = euclideanDistance(queryVector, vector);
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
    std::array<bool, 5> nearest = kNearestNeighbor(queryVector);
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
