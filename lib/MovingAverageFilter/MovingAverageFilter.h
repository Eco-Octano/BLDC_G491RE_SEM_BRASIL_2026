#pragma once
// "#pragma once" é o "include guard": evita que este arquivo seja incluído
// mais de uma vez na mesma unidade de compilação (equivalente ao clássico
// #ifndef MOVING_AVERAGE_FILTER_H / #define ... / #endif).

#include <cstdint>

/**
 * @brief Filtro de média móvel (buffer circular) para suavizar leituras de sensores
 *        (ex.: corrente de fase, velocidade estimada, tensão do barramento DC).
 *
 * CONVENÇÃO DE BIBLIOTECA (PlatformIO):
 *   - Cada biblioteca própria do projeto mora em lib/<NomeDaBiblioteca>/
 *   - Dentro dela: <Nome>.h (interface/declaração) e <Nome>.cpp (implementação)
 *   - O PlatformIO Library Dependency Finder detecta automaticamente quando
 *     você faz #include "MovingAverageFilter.h" em algum arquivo de src/,
 *     não é preciso registrar nada no platformio.ini.
 *
 * REGRA GERAL .h vs .cpp:
 *   - No .h você DECLARA: quais métodos e atributos a classe tem (a "vitrine").
 *   - No .cpp você DEFINE: como cada método efetivamente funciona (a "fábrica").
 *   - Isso permite que outros arquivos usem a classe incluindo só o .h,
 *     sem precisar reprocessar a implementação inteira a cada compilação.
 *
 * Esta classe evita alocação dinâmica (new/delete): em firmware embarcado
 * é preferível reservar tudo estaticamente, evitando fragmentação de heap
 * e comportamento não determinístico.
 */
class MovingAverageFilter
{
public:
    // Tamanho máximo de janela suportado pelo buffer interno.
    // "static constexpr" = constante conhecida em tempo de compilação,
    // compartilhada por todas as instâncias (não ocupa RAM por objeto).
    static constexpr uint16_t kMaxWindowSize = 32;

    /**
     * @param windowSize Quantidade de amostras usadas no cálculo da média.
     *                    É limitada automaticamente ao intervalo [1, kMaxWindowSize].
     */
    explicit MovingAverageFilter(uint16_t windowSize);

    /**
     * @brief Insere uma nova amostra no filtro e retorna a média já atualizada.
     */
    float update(float newSample);

    /// @brief Retorna a última média calculada, sem inserir nova amostra.
    float getAverage() const;

    /// @brief Zera todo o histórico de amostras (volta ao estado inicial).
    void reset();

private:
    uint16_t windowSize_;              // janela efetiva em uso (<= kMaxWindowSize)
    uint16_t sampleCount_;             // quantas amostras válidas já foram inseridas
    uint16_t writeIndex_;              // próxima posição a escrever no buffer circular
    float buffer_[kMaxWindowSize];     // buffer circular de amostras
    float sum_;                        // soma corrente das amostras válidas (média em O(1))
};
