#pragma once

#include "hadronyche/dsp/BandLimitedStep.h"

#include <cmath>

namespace PartialResynth {

using Hadronyche::stepCorrection;

// A band-limited oscillator. The saw and square use a 4-point polynomial correction around each step.
class Oscillator
{
public:
    // The order matches kWaves in Parameters.cpp.
    enum class Wave
    {
        Sine,
        Saw,
        Square
    };

    void reset () { _phase = 0.0; }

    // increment is the frequency in cycles per sample.
    float process (Wave wave, double increment)
    {
        _phase += increment;
        if (_phase >= 1.0)
            _phase -= std::floor (_phase);

        if (wave == Wave::Sine)
            return increment < 0.5 ? static_cast<float> (std::sin (kTwoPi * _phase)) : 0.0f;

        if (increment >= kFadeEnd)
            return 0.0f;

        double value = wave == Wave::Saw ? saw (increment) : square (increment);
        if (increment > kFadeStart)
            value *= (kFadeEnd - increment) / (kFadeEnd - kFadeStart);
        return static_cast<float> (value);
    }

private:
    static constexpr double kTwoPi = 6.283185307179586;

    // Above this, steps come too close together for the correction to deal with them. A saw or square this
    // high is nearly a sine anyway, so fading it out only loses a little at the very top.
    static constexpr double kFadeStart = 0.2;
    static constexpr double kFadeEnd = 0.25;

    // The correction at the wrap, which is a step of +1 in units of its size.
    double wrapCorrection (double increment) const
    {
        const double reach = 2.0 * increment;
        double correction = 0.0;
        if (_phase < reach)
            correction += stepCorrection (_phase / increment);
        if (1.0 - _phase < reach)
            correction += stepCorrection ((_phase - 1.0) / increment);
        return correction;
    }

    double saw (double increment) const
    {
        return 2.0 * _phase - 1.0 - 2.0 * wrapCorrection (increment);
    }

    double square (double increment) const
    {
        double value = (_phase < 0.5 ? 1.0 : -1.0) + 2.0 * wrapCorrection (increment);

        const double fromMiddle = _phase - 0.5;
        if (std::abs (fromMiddle) < 2.0 * increment)
            value -= 2.0 * stepCorrection (fromMiddle / increment);
        return value;
    }

    double _phase { 0.0 };
};

} // namespace PartialResynth
