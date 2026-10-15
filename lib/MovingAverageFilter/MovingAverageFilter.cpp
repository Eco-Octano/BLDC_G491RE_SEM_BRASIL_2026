// A primeira linha de um .cpp de biblioteca SEMPRE inclui o próprio .h
// (usando aspas "..." porque é um header local do projeto/lib, não do sistema).
#include "MovingAverageFilter.h"

// Toda função/método aqui usa o operador de escopo "ClassName::" para dizer
// ao compilador "isto é a implementação do método declarado na classe X".

MovingAverageFilter::MovingAverageFilter(uint16_t windowSize)
    // Lista de inicialização de membros: roda ANTES do corpo do construtor
    // e é a forma preferida de inicializar atributos em C++.
    : windowSize_(windowSize == 0 ? 1
                  : (windowSize > kMaxWindowSize ? kMaxWindowSize : windowSize))
    , sampleCount_(0)
    , writeIndex_(0)
    , buffer_{}          // zera o array inteiro
    , sum_(0.0f)
{
    // Corpo vazio: toda a inicialização já foi feita acima.
}

float MovingAverageFilter::update(float newSample)
{
    if (sampleCount_ < windowSize_)
    {
        // Buffer ainda não está cheio: apenas soma a nova amostra.
        buffer_[writeIndex_] = newSample;
        sum_ += newSample;
        ++sampleCount_;
    }
    else
    {
        // Buffer cheio: descarta a amostra mais antiga (na posição de escrita)
        // e insere a nova no lugar dela -> comportamento de buffer circular.
        sum_ -= buffer_[writeIndex_];
        buffer_[writeIndex_] = newSample;
        sum_ += newSample;
    }

    writeIndex_ = static_cast<uint16_t>((writeIndex_ + 1) % windowSize_);

    return getAverage();
}

float MovingAverageFilter::getAverage() const
{
    if (sampleCount_ == 0)
    {
        return 0.0f;
    }
    return sum_ / static_cast<float>(sampleCount_);
}

void MovingAverageFilter::reset()
{
    sampleCount_ = 0;
    writeIndex_ = 0;
    sum_ = 0.0f;
    // Não precisa zerar o conteúdo de buffer_: como sampleCount_ volta a 0,
    // update() vai sobrescrever tudo antes de qualquer leitura fazer sentido.
}
