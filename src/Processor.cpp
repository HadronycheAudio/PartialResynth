#include "Processor.h"

#include "Controller.h"

#include "hadronyche/dsp/Denormals.h"
#include "hadronyche/dsp/Gain.h"

#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Steinberg;

namespace PartialResynth {

namespace {

constexpr double kSmoothingMs = 20.0;

// How often the voices update what's costly to calculate, in samples.
constexpr int kControlInterval = 16;

// The glide's time is how long it takes to cover 95% of the interval, which is 3 time constants.
constexpr double kGlideTimeConstants = 3.0;

// Below this, a block of input counts as silent. It's about -140 dB.
constexpr float kSilentPeak = 1e-7f;

// Resonance goes from no peak at all to a strong one, evenly in terms of Q.
constexpr double kMinQ = 0.707;
constexpr double kMaxQ = 20.0;

// Across the stereo field when Spread is at 100%. The first voice is centred.
constexpr std::array<double, kVoiceCount> kPanPositions { 0.0, -0.5, 0.5, -1.0, 1.0 };

// Doubling the sample rate doubles the window, which keeps its length in time, and so what it can tell apart.
int windowSizeFor (double sampleRate)
{
    if (sampleRate > 128000.0)
        return 8192;
    return sampleRate > 64000.0 ? 4096 : 2048;
}

// Half the window, because a window's result describes what happened around its centre, then the hop, because
// the voices take that long to reach it. The hop is a quarter window, with the two analyzers staggered.
int latencyFor (int windowSize)
{
    return windowSize / 2 + windowSize / 4;
}

double qFromResonance (double percent)
{
    return kMinQ * std::pow (kMaxQ / kMinQ, percent / 100.0);
}

} // namespace

Processor::Processor () : _params (getParamSpec)
{
    setControllerClass (Controller::getClassFUID ());
}

tresult PLUGIN_API Processor::initialize (FUnknown* context)
{
    const tresult result = AudioEffect::initialize (context);
    if (result != kResultOk)
        return result;

    addAudioInput (STR16 ("Stereo In"), Vst::SpeakerArr::kStereo);
    addAudioOutput (STR16 ("Stereo Out"), Vst::SpeakerArr::kStereo);
    return kResultOk;
}

tresult PLUGIN_API Processor::setActive (TBool state)
{
    if (state)
        prepareDsp ();

    return AudioEffect::setActive (state);
}

tresult PLUGIN_API Processor::setBusArrangements (Vst::SpeakerArrangement* inputs, int32 numIns,
                                                  Vst::SpeakerArrangement* outputs, int32 numOuts)
{
    if (numIns == 1 && numOuts == 1 && inputs[0] == Vst::SpeakerArr::kStereo && outputs[0] == Vst::SpeakerArr::kStereo)
        return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);

    return kResultFalse;
}

tresult PLUGIN_API Processor::canProcessSampleSize (int32 symbolicSampleSize)
{
    return symbolicSampleSize == Vst::kSample32 ? kResultTrue : kResultFalse;
}

uint32 PLUGIN_API Processor::getLatencySamples ()
{
    return static_cast<uint32> (latencyFor (windowSizeFor (processSetup.sampleRate)));
}

// The sound stops at the input's silence plus the buffers, and the silence flags say when.
uint32 PLUGIN_API Processor::getTailSamples ()
{
    const int windowSize = windowSizeFor (processSetup.sampleRate);
    return static_cast<uint32> (windowSize + latencyFor (windowSize));
}

double Processor::plain (ParamId id) const
{
    return _params.plain (id);
}

void Processor::applyParameters ()
{
    const float powerFloor = static_cast<float> (plain (kThreshold)) + FiddleAnalyzer::getFullScalePower (_windowSize);
    for (auto& analyzer : _analyzers)
        analyzer.setPowerFloor (powerFloor);

    const double ticksPerSecond = processSetup.sampleRate / kControlInterval;
    const double glideSeconds = plain (kGlide) / 1000.0 / kGlideTimeConstants;
    _voiceControls.glideCoefficient = glideSeconds > 0.0 ? std::exp (-1.0 / (ticksPerSecond * glideSeconds)) : 0.0;

    _voiceControls.wave1 = static_cast<Oscillator::Wave> (static_cast<int> (plain (kOsc1Wave)));
    _voiceControls.wave2 = static_cast<Oscillator::Wave> (static_cast<int> (plain (kOsc2Wave)));
    _voiceControls.filterMode = static_cast<StateVariableFilter::Mode> (static_cast<int> (plain (kFilterMode)));

    _level1.setTarget (plain (kOsc1Level) / 100.0);
    _level2.setTarget (plain (kOsc2Level) / 100.0);
    _tuning1.setTarget (plain (kOsc1Coarse) + plain (kOsc1Fine) / 100.0);
    _tuning2.setTarget (plain (kOsc2Coarse) + plain (kOsc2Fine) / 100.0);
    _cutoffOctaves.setTarget (std::log2 (plain (kFilterCutoff)));
    _q.setTarget (qFromResonance (plain (kFilterResonance)));
    _keyFollow.setTarget (plain (kFilterKeyFollow) / 100.0);
    _envAmount.setTarget (plain (kFilterEnvAmount));
    _spread.setTarget (plain (kSpread) / 100.0);
    _dryGain.setTarget (Hadronyche::dryGain (plain (kDryWet)));
    _wetGain.setTarget (Hadronyche::wetGain (plain (kDryWet)));
    _volume.setTarget (Hadronyche::volumeToGain (plain (kVolume)));
}

// Moves everything straight to its current target, for when nobody can hear it moving.
void Processor::snapSmoothers ()
{
    for (auto* smoother : { &_level1, &_level2, &_tuning1, &_tuning2, &_cutoffOctaves, &_q, &_keyFollow, &_envAmount, &_spread,
                            &_dryGain, &_wetGain, &_volume })
        smoother->snapToTarget ();
}

void Processor::prepareDsp ()
{
    const double sampleRate = processSetup.sampleRate;
    _windowSize = windowSizeFor (sampleRate);
    const int hop = _windowSize / 4;

    _analyzers[0].prepare (_windowSize, sampleRate);
    _analyzers[1].prepare (_windowSize, sampleRate, hop);
    _tracker.reset ();

    for (auto& voice : _voices)
        voice.prepare (sampleRate, hop);

    const double ticksPerSecond = sampleRate / kControlInterval;
    for (auto* smoother : { &_level1, &_level2, &_tuning1, &_tuning2, &_cutoffOctaves, &_q, &_keyFollow, &_envAmount, &_spread })
        smoother->prepare (ticksPerSecond, kSmoothingMs);
    for (auto* smoother : { &_dryGain, &_wetGain, &_volume })
        smoother->prepare (sampleRate, kSmoothingMs);

    for (auto& channel : _dryDelay)
        channel.assign (static_cast<size_t> (latencyFor (_windowSize)), 0.0f);
    _dryPosition = 0;

    _controlCountdown = 0;
    _silentInputSamples = 0;
    _isFirstBlock = true;

    applyParameters ();
    snapSmoothers ();
}

void Processor::updateControls ()
{
    _voiceControls.level1 = static_cast<float> (_level1.next ());
    _voiceControls.level2 = static_cast<float> (_level2.next ());
    _voiceControls.ratio1 = std::pow (2.0, _tuning1.next () / 12.0);
    _voiceControls.ratio2 = std::pow (2.0, _tuning2.next () / 12.0);
    _voiceControls.cutoffOctaves = _cutoffOctaves.next ();
    _voiceControls.q = _q.next ();
    _voiceControls.keyFollow = _keyFollow.next ();
    _voiceControls.envAmount = _envAmount.next ();

    // At constant power, with the gains scaled to reach 1 in the middle, so that with Spread at 0, the voices
    // are at the same level in both channels as they'd be in mono.
    const double spread = _spread.next ();
    for (size_t v = 0; v < kVoiceCount; ++v)
    {
        const double angle = 0.7853981633974483 * (1.0 + spread * kPanPositions[v]);
        _panLeft[v] = static_cast<float> (1.4142135623730951 * std::cos (angle));
        _panRight[v] = static_cast<float> (1.4142135623730951 * std::sin (angle));
        _voices[v].updateControls (_voiceControls);
    }
}

void Processor::render (const float* inputLeft, const float* inputRight, float* outputLeft, float* outputRight, int32 count)
{
    const size_t delayLength = _dryDelay[0].size ();

    for (int32 i = 0; i < count; ++i)
    {
        // A broken sample upstream would otherwise stay in the envelopes and filters until reactivation.
        const float left = inputLeft && std::isfinite (inputLeft[i]) ? inputLeft[i] : 0.0f;
        const float right = inputRight && std::isfinite (inputRight[i]) ? inputRight[i] : 0.0f;

        const float mono = 0.5f * (left + right);
        for (auto& analyzer : _analyzers)
        {
            if (analyzer.push (mono))
            {
                const auto& targets = _tracker.update (analyzer.getPeaks (), analyzer.getPeakCount ());
                for (size_t v = 0; v < kVoiceCount; ++v)
                    _voices[v].setTarget (targets[v], _voiceControls);
            }
        }

        if (--_controlCountdown < 0)
        {
            _controlCountdown = kControlInterval - 1;
            updateControls ();
        }

        float wetLeft = 0.0f;
        float wetRight = 0.0f;
        for (size_t v = 0; v < kVoiceCount; ++v)
        {
            const float voice = _voices[v].process (_voiceControls);
            wetLeft += voice * _panLeft[v];
            wetRight += voice * _panRight[v];
        }

        const float dryLeft = _dryDelay[0][_dryPosition];
        const float dryRight = _dryDelay[1][_dryPosition];
        _dryDelay[0][_dryPosition] = left;
        _dryDelay[1][_dryPosition] = right;
        _dryPosition = _dryPosition + 1 == delayLength ? 0 : _dryPosition + 1;

        const auto dryGain = static_cast<float> (_dryGain.next ());
        const auto wetGain = static_cast<float> (_wetGain.next ());
        const auto volume = static_cast<float> (_volume.next ());
        outputLeft[i] = (dryLeft * dryGain + wetLeft * wetGain) * volume;
        outputRight[i] = (dryRight * dryGain + wetRight * wetGain) * volume;
    }
}

tresult PLUGIN_API Processor::process (Vst::ProcessData& data)
{
    _params.readChanges (data.inputParameterChanges);

    if (data.numOutputs == 0 || data.numSamples == 0)
        return kResultOk;

    auto& output = data.outputs[0];
    if (output.numChannels != 2)
    {
        for (int32 c = 0; c < output.numChannels; ++c)
            std::memset (output.channelBuffers32[c], 0, sizeof (float) * static_cast<size_t> (data.numSamples));
        return kResultOk;
    }

    const Hadronyche::FlushDenormals flushDenormals;
    applyParameters ();

    // Changes arriving with the first block are where the plug-in starts, not something to ramp to.
    if (_isFirstBlock)
    {
        snapSmoothers ();
        _isFirstBlock = false;
    }

    const float* inputLeft = nullptr;
    const float* inputRight = nullptr;
    if (data.numInputs > 0 && data.inputs[0].numChannels == 2)
    {
        inputLeft = data.inputs[0].channelBuffers32[0];
        inputRight = data.inputs[0].channelBuffers32[1];
    }

    // Looks at the samples rather than the host's silence flags, which are only ever a hint.
    float inputPeak = 0.0f;
    if (inputLeft)
    {
        for (int32 i = 0; i < data.numSamples; ++i)
            inputPeak = std::max ({ inputPeak, std::abs (inputLeft[i]), std::abs (inputRight[i]) });
    }

    _silentInputSamples = inputPeak < kSilentPeak ? _silentInputSamples + data.numSamples : 0;

    // By now, the analyzers and the dry delay can only hold silence, so nothing here can make a sound.
    const bool isAsleep = _silentInputSamples >= _windowSize + latencyFor (_windowSize) + data.numSamples
                          && std::all_of (_voices.begin (), _voices.end (), [] (const Voice& voice) { return voice.isSilent (); });
    if (isAsleep)
    {
        for (auto& analyzer : _analyzers)
            analyzer.skipSilence (data.numSamples);
        _controlCountdown = (_controlCountdown + kControlInterval - data.numSamples % kControlInterval) % kControlInterval;

        // Nobody can hear parameters move while asleep, and sound shouldn't start with them still ramping.
        snapSmoothers ();

        std::memset (output.channelBuffers32[0], 0, sizeof (float) * static_cast<size_t> (data.numSamples));
        std::memset (output.channelBuffers32[1], 0, sizeof (float) * static_cast<size_t> (data.numSamples));
        output.silenceFlags = 3;
        return kResultOk;
    }

    render (inputLeft, inputRight, output.channelBuffers32[0], output.channelBuffers32[1], data.numSamples);
    output.silenceFlags = 0;
    return kResultOk;
}

tresult PLUGIN_API Processor::setState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    const bool ok = readParamState (state, [this] (Vst::ParamID id, double value) { _params[id] = value; });
    return ok ? kResultOk : kResultFalse;
}

tresult PLUGIN_API Processor::getState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    return writeParamState (state, _params.snapshot ()) ? kResultOk : kResultFalse;
}

} // namespace PartialResynth
