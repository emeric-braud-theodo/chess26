#include "engine/eval/nnue/accumulator_layer.hpp"

template <int NFeatures, int NNeurons>
template <int nFeature, bool activate>
inline void AccumulatorLayer<NFeatures, NNeurons>::update_feature()
{
    static_assert(nFeature >= 0, "Feature index must be non-negative");
    static_assert(nFeature < NFeatures, "Feature out of index bounds");
    for (int j = 0; j < NNeurons; ++j)
    {
        if constexpr (activate)
            accumulator[j] += this->weights[nFeature][j];
        else
            accumulator[j] -= this->weights[nFeature][j];
    }
}