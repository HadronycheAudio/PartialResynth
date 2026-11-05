#include "Parameters.h"

#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>

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
    { "Volume", "dB", kVolumeSilenceDb, 6.0, 0.0, Curve::Linear, 1 },
} };

} // namespace

const ParamSpec& getParamSpec (Vst::ParamID id)
{
    return kSpecs[id];
}

double normalizedToPlain (const ParamSpec& spec, double normalized)
{
    const double n = std::clamp (normalized, 0.0, 1.0);
    if (spec.curve == Curve::List)
        return std::round (n * (spec.labelCount - 1));
    if (spec.curve == Curve::Integer)
        return std::round (spec.min + (spec.max - spec.min) * n);
    if (spec.curve == Curve::Log)
        return spec.min * std::pow (spec.max / spec.min, n);
    if (spec.curve == Curve::Squared)
        return spec.min + (spec.max - spec.min) * n * n;
    return spec.min + (spec.max - spec.min) * n;
}

double plainToNormalized (const ParamSpec& spec, double plain)
{
    const double p = std::clamp (plain, spec.min, spec.max);
    if (spec.curve == Curve::Log)
        return std::log (p / spec.min) / std::log (spec.max / spec.min);
    if (spec.curve == Curve::Squared)
        return std::sqrt ((p - spec.min) / (spec.max - spec.min));
    return (p - spec.min) / (spec.max - spec.min);
}

bool writeParamState (IBStream* stream, const ParamValues& normalizedValues)
{
    IBStreamer streamer (stream, kLittleEndian);
    if (!streamer.writeInt32 (kStateVersion) || !streamer.writeInt32 (kParamCount))
        return false;

    for (int32 id = 0; id < kParamCount; ++id)
    {
        if (!streamer.writeInt32 (id) || !streamer.writeDouble (normalizedValues[static_cast<size_t> (id)]))
            return false;
    }
    return true;
}

bool readParamState (IBStream* stream, const std::function<void (Vst::ParamID, double)>& onValue)
{
    IBStreamer streamer (stream, kLittleEndian);

    int32 version = 0;
    int32 count = 0;
    if (!streamer.readInt32 (version) || version < 1 || version > kStateVersion || !streamer.readInt32 (count))
        return false;

    for (int32 i = 0; i < count; ++i)
    {
        int32 id = 0;
        double value = 0.0;
        if (!streamer.readInt32 (id) || !streamer.readDouble (value))
            return false;

        if (id >= 0 && id < kParamCount && std::isfinite (value))
            onValue (static_cast<Vst::ParamID> (id), std::clamp (value, 0.0, 1.0));
    }
    return true;
}

} // namespace PartialResynth
