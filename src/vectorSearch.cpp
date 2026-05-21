#include "vectorSearch.hpp"


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
}

const std::vector<Reference>& getReferences(){
    // se não carregou, carrega as referências e mantém em memória
}


std::array<bool, 5> kNearestNeighbor(const std::array<float, 14>& queryVector){
    // carregar/receber references
    // para cada Reference ref:
    //      distance = euclideanDistance(queryVector, ref.vector)
    //      se distance estiver entre as 5 menores:
    //          guardar Neighbor{distance, ref.fraud}
    // retornar os 5 menores
}

// transactionIsApproved()
// fraudScore = quantidade de frauds entre os 5 / 5.0f
// approved = fraudScore < 0.6f