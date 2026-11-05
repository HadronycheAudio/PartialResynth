#pragma once

#include "Oscillator.h"
#include "PartialTracker.h"
#include "StateVariableFilter.h"

#include <algorithm>
#include <cmath>

namespace PartialResynth {

// What every voice shares, worked out once per control tick. One set of controls plays all the voices.
struct VoiceControls
{
    Oscillator::Wave wave1 { Oscillator::Wave::Sine };
    Oscillator::Wave wave2 { Oscillator::Wave::Saw };
    float level1 { 1.0f };
    float level2 { 0.0f };
    double ratio1 { 1.0 }; // The oscillators' tuning, as a multiple of the partial's frequency.
    double ratio2 { 1.0 };

    StateVariableFilter::Mode filterMode { StateVariableFilter::Mode::LowPass };
    double cutoffOctaves { 0.0 }; // log2 of the frequency, so that the modulation below just adds to it.
    double q { 0.707 };
    double keyFollow { 0.0 };
    double envAmount { 0.0 };

    double glideCoefficient { 0.0 }; // Per control tick. Zero means no glide.
};

// One partial's synth voice: two oscillators into a filter, then the level from the analysis.
//
// The level comes last so that the filter has no tail: when the partial's gone, so is the voice's sound.
class Voice
{
public:
    void prepare (double sampleRate, int rampSamples)
    {
        _sampleRate = sampleRate;
        _rampSamples = rampSamples;
        _filter.prepare (sampleRate);
        _oscillator1.reset ();
        _oscillator2.reset ();
        _amplitude = 0.0f;
        _amplitudeStep = 0.0f;
        _pitch = 0.0;
        _pitchStep = 0.0;
        _gliding = 0.0;
        _framesBeforeGlide = 0;
        _rampLeft = 0;
    }

    // From the analysis, at the start of a ramp lasting until the next frame's values arrive. Amplitude ramps
    // linearly, as the envelope, and pitch ramps in octaves.
    void setTarget (const PartialTracker::Target& target, const VoiceControls& controls)
    {
        if (target.amplitude == 0.0f && isSilent ())
            return;

        _amplitudeStep = (target.amplitude - _amplitude) / static_cast<float> (_rampSamples);
        _amplitudeTarget = target.amplitude;

        if (target.frequency > 0.0f)
        {
            const double pitch = std::log2 (static_cast<double> (target.frequency));
            if (target.isNew)
            {
                // Nothing can be heard from before this, so it starts on its pitch, not gliding to it.
                _pitch = pitch;
                _pitchStep = 0.0;
                _gliding = pitch;
                _framesBeforeGlide = kFramesToFillWindow;
                _filter.reset ();
            }
            else
            {
                _pitchStep = (pitch - _pitch) / _rampSamples;
                _framesBeforeGlide = std::max (_framesBeforeGlide - 1, 0);
            }
        }
        _rampLeft = _rampSamples;

        // So that its first samples don't use what was worked out for the partial it played before.
        if (target.isNew)
            updateControls (controls);
    }

    bool isSilent () const { return _amplitude == 0.0f && _rampLeft == 0; }

    // A coarser rate than the audio, for what's costly to calculate and can't be heard moving at this speed.
    void updateControls (const VoiceControls& controls)
    {
        if (isSilent ())
            return;

        // A partial's first frames see it through a part-filled window, which puts its pitch slightly off. A
        // long glide would hold on to that, so it starts once the window is full of the partial.
        _gliding = _framesBeforeGlide > 0 ? _pitch : _pitch + (_gliding - _pitch) * controls.glideCoefficient;

        const double frequency = std::pow (2.0, _gliding) / _sampleRate;
        _increment1 = frequency * controls.ratio1;
        _increment2 = frequency * controls.ratio2;

        // In terms of level, so that a quiet partial moves the filter as well, rather than hardly at all.
        const double envelope = std::clamp (1.0 + 20.0 * std::log10 (std::max (_amplitude, 1e-6f)) / kEnvelopeRangeDb, 0.0, 1.0);

        const double octaves = controls.cutoffOctaves + controls.keyFollow * (_gliding - kKeyFollowCentreOctaves) + controls.envAmount * envelope;
        _filter.setCoefficients (std::pow (2.0, octaves), controls.q);
    }

    float process (const VoiceControls& controls)
    {
        if (isSilent ())
            return 0.0f;

        float sum = 0.0f;
        if (controls.level1 > kLevelOff)
            sum += controls.level1 * _oscillator1.process (controls.wave1, _increment1);
        if (controls.level2 > kLevelOff)
            sum += controls.level2 * _oscillator2.process (controls.wave2, _increment2);

        const float output = _filter.process (sum, controls.filterMode) * _amplitude;

        if (_rampLeft > 0)
        {
            _amplitude += _amplitudeStep;
            _pitch += _pitchStep;
            if (--_rampLeft == 0)
                _amplitude = _amplitudeTarget; // Exact, because adding up steps wouldn't reach zero exactly.
        }
        return output;
    }

private:
    static constexpr double kEnvelopeRangeDb = 60.0;
    static constexpr double kKeyFollowCentreOctaves = 8.781359713524660; // log2 (440)
    static constexpr float kLevelOff = 1e-5f;
    static constexpr int kFramesToFillWindow = 4; // A frame arrives every quarter of a window.

    double _sampleRate { 44100.0 };
    int _rampSamples { 1 };

    Oscillator _oscillator1;
    Oscillator _oscillator2;
    StateVariableFilter _filter;

    float _amplitude { 0.0f };
    float _amplitudeStep { 0.0f };
    float _amplitudeTarget { 0.0f };
    double _pitch { 0.0 }; // log2 of the frequency in Hz.
    double _pitchStep { 0.0 };
    double _gliding { 0.0 }; // _pitch after the glide.
    int _framesBeforeGlide { 0 };
    int _rampLeft { 0 };

    double _increment1 { 0.0 };
    double _increment2 { 0.0 };
};

} // namespace PartialResynth
