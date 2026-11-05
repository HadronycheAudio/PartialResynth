#pragma once

#include <algorithm>
#include <cmath>

namespace PartialResynth {

// A mono topology-preserving state-variable filter, which stays stable under fast modulation.
class StateVariableFilter
{
public:
    // The order matches kFilterModes in Parameters.cpp.
    enum class Mode
    {
        LowPass,
        HighPass,
        BandPass,
        Notch
    };

    void prepare (double sampleRate)
    {
        _sampleRate = sampleRate;
        reset ();
    }

    void reset ()
    {
        _ic1 = 0.0f;
        _ic2 = 0.0f;
    }

    void setCoefficients (double frequency, double q)
    {
        const double limited = std::clamp (frequency, 10.0, _sampleRate * 0.49);
        const double g = std::tan (3.141592653589793 * limited / _sampleRate);
        const double k = 1.0 / q;

        _k = static_cast<float> (k);
        const double a1 = 1.0 / (1.0 + g * (g + k));
        const double a2 = g * a1;
        _a1 = static_cast<float> (a1);
        _a2 = static_cast<float> (a2);
        _a3 = static_cast<float> (g * a2);
    }

    float process (float input, Mode mode)
    {
        const float v3 = input - _ic2;
        const float v1 = _a1 * _ic1 + _a2 * v3;
        const float v2 = _ic2 + _a2 * _ic1 + _a3 * v3;
        _ic1 = 2.0f * v1 - _ic1;
        _ic2 = 2.0f * v2 - _ic2;

        switch (mode)
        {
            case Mode::LowPass: return v2;
            case Mode::HighPass: return input - _k * v1 - v2;
            case Mode::BandPass: return _k * v1; // Scaled so the peak stays at unity as resonance rises.
            case Mode::Notch: return input - _k * v1;
        }
        return input;
    }

private:
    double _sampleRate { 44100.0 };
    float _k { 1.0f };
    float _a1 { 0.0f };
    float _a2 { 0.0f };
    float _a3 { 0.0f };
    float _ic1 { 0.0f };
    float _ic2 { 0.0f };
};

} // namespace PartialResynth
