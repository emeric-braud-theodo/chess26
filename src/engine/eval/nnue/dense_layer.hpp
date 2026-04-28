#pragma once

#include <array>
#include <cstdint>

template <int NInputs, int NNeurons>
class DenseLayer
{
    alignas(64) std::array<std::array<std::int8_t, NInputs>, NNeurons> weights;
    alignas(64) std::array<std::int32_t, NNeurons> biases;

    int8_t relu(int32_t acc);

public:
    void process(const std::array<int8_t, NInputs> &inputs, std::array<std::int8_t, NNeurons> &output);
    std::int32_t get_result(const std::array<int8_t, NInputs> &inputs)
        requires(NNeurons == 1);
};
