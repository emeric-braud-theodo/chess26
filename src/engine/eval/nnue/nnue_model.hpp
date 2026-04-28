#pragma once

#include <array>
#include <cstdint>
#include <tuple>

#include "engine/eval/nnue/accumulator_layer.hpp"
#include "engine/eval/nnue/dense_layer.hpp"

template <int...>
struct DenseLayerTuple;

template <int In, int Out, int... Rest>
struct DenseLayerTuple<In, Out, Rest...>
{
    using type = decltype(std::tuple_cat(
        std::declval<std::tuple<DenseLayer<In, Out>>>(),
        std::declval<typename DenseLayerTuple<Out, Rest...>::type>()));
};

template <int Last>
struct DenseLayerTuple<Last>
{
    using type = std::tuple<>;
};

template <int NFeatures, int NAccumulator, int... DenseLayers>
class NnueModel
{
    AccumulatorLayer<NFeatures, NAccumulator> accumulator;
    using LayersTuple =
        typename DenseLayerTuple<NAccumulator, DenseLayers...>::type;

    LayersTuple dense_layers;

public:
    int32_t get_result();
};
