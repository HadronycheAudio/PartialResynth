#pragma once

#include "PluginConfig.h"

#include "hadronyche/vst/RustProcessor.h"

namespace PartialResynth {

// The VST3 audio component. The signal path described in docs/signal-path.md runs on the Rust side, in rust/, and
// this is its class ID.
class Processor : public Hadronyche::RustProcessor
{
public:
    Processor ();

    static const Steinberg::FUID getClassFUID () { return Steinberg::FUID (PARTIALRESYNTH_PROCESSOR_UID); }
    static Steinberg::FUnknown* createInstance (void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*> (new Processor);
    }
};

} // namespace PartialResynth
