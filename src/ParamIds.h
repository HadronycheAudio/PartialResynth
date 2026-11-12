#pragma once

// Generated from the parameter table in rust/, by cargo test there. Don't edit it here: change the table,
// and run HADRONYCHE_UPDATE_HEADERS=1 cargo test in rust/ to bring this up to date.

#include "pluginterfaces/vst/vsttypes.h"

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
    kPartials,
    kParamCount
};

} // namespace PartialResynth
