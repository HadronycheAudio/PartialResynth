#include "Parameters.h"

#include "hadronyche/vst/ParamState.h"

using namespace Steinberg;

namespace PartialResynth {

namespace {

constexpr int32 kStateVersion = 1;

// Saved in projects by position, so only ever add to the end of these. The order of kWaves matches
// Oscillator::Wave, and the order of kFilterModes matches StateVariableFilter::Mode.
const char* const kWaves[] = { "Sine", "Saw", "Square" };
constexpr int kWaveCount = 3;
const char* const kFilterModes[] = { "Low-pass", "High-pass", "Band-pass", "Notch" };
constexpr int kFilterModeCount = 4;

// Listed in ParamId order.
const std::array<ParamSpec, kParamCount> kSpecs { {
    { "Threshold", "dB", -90.0, -20.0, -60.0, Curve::Linear, 0 },
    { "Glide", "ms", 0.0, 2000.0, 0.0, Curve::Squared, 0 },

    { "Osc 1 wave", "", 0.0, kWaveCount - 1, 0.0, Curve::List, 0, kWaves, kWaveCount },
    { "Osc 1 level", "%", 0.0, 100.0, 100.0, Curve::Linear, 0 },
    { "Osc 1 coarse", "st", -24.0, 24.0, 0.0, Curve::Integer, 0 },
    { "Osc 1 fine", "ct", -100.0, 100.0, 0.0, Curve::Linear, 0 },

    { "Osc 2 wave", "", 0.0, kWaveCount - 1, 1.0, Curve::List, 0, kWaves, kWaveCount },
    { "Osc 2 level", "%", 0.0, 100.0, 0.0, Curve::Linear, 0 },
    { "Osc 2 coarse", "st", -24.0, 24.0, 0.0, Curve::Integer, 0 },
    { "Osc 2 fine", "ct", -100.0, 100.0, 0.0, Curve::Linear, 0 },

    { "Filter mode", "", 0.0, kFilterModeCount - 1, 0.0, Curve::List, 0, kFilterModes, kFilterModeCount },
    { "Filter cutoff", "Hz", 20.0, 20000.0, 20000.0, Curve::Log, 0 },
    { "Filter resonance", "%", 0.0, 100.0, 0.0, Curve::Linear, 0 },
    { "Filter key follow", "%", 0.0, 100.0, 0.0, Curve::Linear, 0 },
    { "Filter env amount", "oct", -8.0, 8.0, 0.0, Curve::Linear, 2 },

    { "Spread", "%", 0.0, 100.0, 0.0, Curve::Linear, 0 },
    { "Dry/wet", "%", 0.0, 100.0, 100.0, Curve::Linear, 0 },
    { "Volume", "dB", kVolumeSilenceDb, 6.0, 0.0, Curve::Linear, 1, nullptr, 0, true },
} };

} // namespace

const ParamSpec& getParamSpec (Vst::ParamID id)
{
    return kSpecs[id];
}

bool writeParamState (IBStream* stream, const ParamValues& normalizedValues)
{
    return Hadronyche::writeParamState (stream, kStateVersion, normalizedValues.data (), kParamCount);
}

bool readParamState (IBStream* stream, const std::function<void (Vst::ParamID, double)>& onValue)
{
    return Hadronyche::readParamState (stream, kStateVersion, kParamCount,
                                       [&onValue] (Vst::ParamID id, double value, int32) { onValue (id, value); });
}

} // namespace PartialResynth
