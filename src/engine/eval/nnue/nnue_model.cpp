#include "engine/eval/nnue/nnue_model.hpp"

template <int NFeatures, int NAccumulator, int... DenseLayers>
inline int32_t NnueModel<NFeatures, NAccumulator, DenseLayers...>::get_result()
{
    static_assert(sizeof(... DenseLayers) >= 2);
    int32_t acc = 0;
    std::get return 0;
}
