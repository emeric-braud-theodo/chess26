#include "engine/eval/nnue/dense_layer.hpp"

#include <cstdint>
#include <array>
#include <algorithm>
#include "dense_layer.hpp"

template <int NInputs, int NNeurons>
inline int8_t DenseLayer<NInputs, NNeurons>::relu(int32_t acc)
{
    return static_cast<std::int8_t>(std::clamp(acc, 0, 127));
}

template <int NInputs, int NNeurons>
inline void DenseLayer<NInputs, NNeurons>::process(const std::array<int8_t, NInputs> &inputs, std::array<std::int8_t, NNeurons> &output)
{
    for (int j = 0; j < NNeurons; ++j)
    {
        int32_t acc = this->biases[j];
        for (int i = 0; i < NInputs; ++i)
        {
            acc += inputs[i] * weights[j][i];
        }
        output[j] = this->relu(acc);
    }
}
template <int NInputs, int NNeurons>
std::int32_t DenseLayer<NInputs, NNeurons>::get_result(const std::array<int8_t, NInputs> &inputs)
    requires(NNeurons == 1)
{
    int32_t acc = this->biases[0];
    for (int i = 0; i < NInputs; ++i)
    {
        acc += inputs[i] * weights[0][i];
    }

    return acc;
}