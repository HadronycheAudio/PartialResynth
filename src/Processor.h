#pragma once

#include "Parameters.h"
#include "PluginConfig.h"
#include "dsp/FiddleAnalyzer.h"
#include "dsp/PartialTracker.h"
#include "dsp/Voice.h"

#include "hadronyche/dsp/Smoother.h"
#include "hadronyche/vst/StereoProcessor.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace PartialResynth {

// The VST3 audio component, which runs the signal path described in docs/signal-path.md.
class Processor : public Hadronyche::StereoProcessor<kParamCount>
{
public:
    Processor ();

    static const Steinberg::FUID getClassFUID () { return Steinberg::FUID (PARTIALRESYNTH_PROCESSOR_UID); }
    static Steinberg::FUnknown* createInstance (void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*> (new Processor);
    }

    Steinberg::uint32 PLUGIN_API getLatencySamples () SMTG_OVERRIDE;
    Steinberg::uint32 PLUGIN_API getTailSamples () SMTG_OVERRIDE;

private:
    void prepareDsp () override;
    void renderBlock (Steinberg::Vst::ProcessData& data, Steinberg::Vst::AudioBusBuffers& output) override;

    double plain (ParamId id) const;
    void applyParameters ();
    void snapSmoothers ();
    void updateControls ();
    void render (const float* inputLeft, const float* inputRight, float* outputLeft, float* outputRight, Steinberg::int32 count);

    using Smoother = Hadronyche::Smoother;

    int _windowSize { 2048 };

    // Two copies of fiddle~, half a hop apart, so that together they give a frame every quarter window.
    std::array<FiddleAnalyzer, 2> _analyzers;
    PartialTracker _tracker;
    std::array<Voice, kVoiceCount> _voices;
    VoiceControls _voiceControls;
    std::array<float, kVoiceCount> _panLeft {};
    std::array<float, kVoiceCount> _panRight {};
    int _controlCountdown { 0 };

    // Per control tick.
    Smoother _level1;
    Smoother _level2;
    Smoother _tuning1; // In semitones.
    Smoother _tuning2;
    Smoother _cutoffOctaves;
    Smoother _q;
    Smoother _keyFollow;
    Smoother _envAmount;
    Smoother _spread;

    // Per sample.
    Smoother _dryGain;
    Smoother _wetGain;
    Smoother _volume;

    // Delays the dry signal by the plug-in's latency, so that it lines up with the voices.
    std::array<std::vector<float>, 2> _dryDelay;
    size_t _dryPosition { 0 };

    // With nothing left to hear, and nothing left in the buffers, the plug-in stops working.
    int64_t _silentInputSamples { 0 };
    bool _isFirstBlock { true };
};

} // namespace PartialResynth
