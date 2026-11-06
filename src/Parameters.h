#pragma once

#include "hadronyche/dsp/Gain.h"
#include "hadronyche/vst/ParamSpec.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <array>
#include <functional>

namespace PartialResynth {

// Parameter IDs are saved in projects and automation: never renumber them, only append.
enum ParamId : Steinberg::Vst::ParamID
{
    kThreshold,
    kGlide,
    kOsc1Wave,
    kOsc1Level,
    kOsc1Coarse,
    kOsc1Fine,
    kOsc2Wave,
    kOsc2Level,
    kOsc2Coarse,
    kOsc2Fine,
    kFilterMode,
    kFilterCutoff,
    kFilterResonance,
    kFilterKeyFollow,
    kFilterEnvAmount,
    kSpread,
    kDryWet,
    kVolume,
    kParamCount
};

using Hadronyche::Curve;
using Hadronyche::ParamSpec;
using Hadronyche::kVolumeSilenceDb;
using Hadronyche::normalizedToPlain;
using Hadronyche::plainToNormalized;

const ParamSpec& getParamSpec (Steinberg::Vst::ParamID id);

using ParamValues = std::array<double, kParamCount>;

bool writeParamState (Steinberg::IBStream* stream, const ParamValues& normalizedValues);

// Calls onValue (id, normalized) for every known parameter in the stream.
bool readParamState (Steinberg::IBStream* stream, const std::function<void (Steinberg::Vst::ParamID, double)>& onValue);

} // namespace PartialResynth
