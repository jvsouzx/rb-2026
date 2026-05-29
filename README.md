# Rinha de Backend - 2026 - Detecção de Fraude

1. Entender as restrições
2. Escolha da stack, escolha de pacotes/libs
3. Criação dos endpoints
4. Entendendo o payload
5. Vetorização / Normalização
6. Estudo do kNN e implementação inicial
7. Início das Otimizações:
    1. Cache na leitura e carregamento inicial de mcc-risk
    2. Pré processamento, load na imagem e carregamento inicial de references.json.gz -> references.bin
    3. mmap (dica)

## Porquê usar SoA para armazenar o references.bin?
Qual a diferença de SoA e AoS e porque isso faz diferença nesse caso?

## Quantização 
float32 -> int8
O que é quantização? 
Porque é útitl?
Como implementar?

Quantização Linear de float32 para uint8_t com escala 1/254, 255 reservado como sentinela para valores < 0; Diminuiu de 164 MiB para 43 MiB. Antes o references.bin estava sendo carregado e estourando os 150MiB de memória de cada instância da API.

A lógica do cálculo da distância euclidiana agora também é feita com aritmética de inteiros, que é mais rápida.

## O que é o `mmap`?

`mmap` é uma syscall que associa uma região do endereço de memória virtual de um processo a uma fonte de dados (pode ser um arquivo ou uma memória anônima) de modo que esses endereços virtuais sejam servidos pelo kernel a partir daquela fonte alocando páginas físicas sob demanda.

Ele não aloca páginas na memória física, a tabela de páginas (page table) só anota: essa faixa de end. virtual aponta pro arquivo x, offset y;

Quando o processo tenta acessar a memória virtual o hardware (MMU) consulta a tabela e descobre que a página não está na RAM, isso dispara um page fault. O kernel intercepta esse page fault, carrega a página requisitada pro page cahce, atualiza a tabela de página e o programa continua rodando como se nada tivesse acontecido.

O mmap é muito útil quando temos por exemplo um arquivo com muitos registros que não são modificados durante a execução mas serão consultados por mais de um processo.

## Conteúdos estudados/a estudar
- Alocação de memória, como processos usam memória, arquitetura de computadores
- SoA vs AoS
- Quantização
- Operações com binários


