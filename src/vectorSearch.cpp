#include "vectorSearch.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <cmath>

using json = nlohmann::json;

std::vector<Reference> loadReferences(const std::string& path){
    // retorna um vetor com as referências carregadas
    // abordagem inicial (não otimizada para o cenário com 3M de refs)

    // 1. abre o arquivo
    // 2. faz o parse do JSON
    // 3. percorre cada entrada do array
    // 4. le o campo "vector" e copia para std::array<float, 14>
    // 5. le o campo "label" e converte para bool fraud
    // 6. cria um Reference com vector + fraud
    // 7. armazena no std::vector<Reference>
    // 8. retorna o vetor

    std::ifstream file(path);
    json jsonFile = json::parse(file);
    std::vector<Reference> references = {};

    for (const auto& item:jsonFile) {
        const auto& jsonVector = item["vector"];
        std::string label = item["label"];

        std::array<float, 14> vector = jsonVector;
        bool fraud = label == "fraud";

        Reference ref = {};

        ref.vector = vector;
        ref.fraud = fraud;

        references.insert(references.end(), ref);
    }
    return references;
}

const std::vector<Reference>& getReferences(){
    // se não carregou, carrega as referências e mantém em memória
    static const std::vector<Reference> references = loadReferences("resources/example-references.json");
    return references;
}

float euclideanDistance(const std::array<float, 14>& queryVector, const std::array<float, 14>& referenceVector) {

    float distance = 0.0f;
    for (int i = 0; i < 14; i++){
        float diff = (referenceVector[i] - queryVector[i]);
        distance += diff * diff;
    }
    return std::sqrt(distance);
}

std::array<bool, 5> kNearestNeighbor(const std::array<float, 14>& queryVector){
    // carregar/receber references
    // para cada Reference ref:
    //      distance = euclideanDistance(queryVector, ref.vector)
    //      se distance estiver entre as 5 menores:
    //          guardar Neighbor{distance, ref.fraud}
    // retornar os 5 menores
    const auto& refs = getReferences();
    std::priority_queue<Neighbor, std::vector<Neighbor>, CompareNeighbor> nearest;

    for (const auto& ref : refs){
        float distance = euclideanDistance(queryVector, ref.vector);
        Neighbor candidate{distance, ref.fraud};
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
