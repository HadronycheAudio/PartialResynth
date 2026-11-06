#pragma once

#include "hadronyche/dsp/Svf.h"

namespace PartialResynth {

// A mono topology-preserving state-variable filter, which stays stable under fast modulation.
class StateVariableFilter
{
public:
    // The order matches kFilterModes in Parameters.cpp.
    using Mode = Hadronyche::SvfMode;

    void prepare (double sampleRate)
    {
        _sampleRate = sampleRate;
        reset ();
    }

    void reset () { _state = {}; }

    void setCoefficients (double frequency, double q)
    {
        _coefficients = Hadronyche::svfCoefficients (Hadronyche::svfWarp (frequency, _sampleRate), q);
    }

    float process (float input, Mode mode)
    {
        return Hadronyche::svfOutput (mode, input, _coefficients.k, Hadronyche::svfTick (input, _coefficients, _state));
    }

private:
    double _sampleRate { 44100.0 };
    Hadronyche::SvfCoefficients _coefficients;
    Hadronyche::SvfState _state;
};

} // namespace PartialResynth
