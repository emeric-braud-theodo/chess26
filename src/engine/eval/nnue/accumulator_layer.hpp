#pragma once

#include <array>
#include <cstdint>
#include "engine/eval/nnue/dense_layer.hpp"

template <int NFeatures, int NNeurons>
class AccumulatorLayer
{
    std::array<std::array<std::int8_t, NNeurons>, NFeatures> weights;
    std::array<std::int16_t, NNeurons> accumulator;

public:
    AccumulatorLayer(std::array<int16_t, NNeurons> biaises) : accumulator(biaises)
    {
    }
    template <int nFeature, bool activate>
    void update_feature();

    const std::array<std::int16_t, NNeurons> &get_accumulator()
    {
        return &accumulator;
    }
};
