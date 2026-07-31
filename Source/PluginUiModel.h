#pragma once

#include <algorithm>

namespace stacksampler::ui
{
struct NormalisedRange
{
    float start = 0.0f;
    float end = 1.0f;
};

inline NormalisedRange visualTrimRange (float sourceStart,
                                        float sourceEnd,
                                        bool reverse) noexcept
{
    const auto start = std::clamp (sourceStart, 0.0f, 0.99f);
    const auto end = std::clamp (sourceEnd, start + 0.01f, 1.0f);
    return reverse ? NormalisedRange { 1.0f - end, 1.0f - start }
                   : NormalisedRange { start, end };
}

inline float sourcePositionFromVisual (float visualPosition, bool reverse) noexcept
{
    const auto position = std::clamp (visualPosition, 0.0f, 1.0f);
    return reverse ? 1.0f - position : position;
}
}
