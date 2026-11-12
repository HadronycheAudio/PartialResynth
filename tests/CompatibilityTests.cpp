// These protect projects people have saved. The expected values are written out here on purpose, rather
// than taken from the plug-in's own definitions, so that changing those definitions fails a test.

#include "Harness.h"
#include "Tests.h"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/common/memorystream.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace Steinberg;

namespace PartialResynth {
namespace Tests {

namespace {

// Hosts save these numbers, so they can never change. New parameters get new numbers.
static_assert (kThreshold == 0 && kGlide == 1, "Parameter IDs are saved in projects");
static_assert (kOsc1Wave == 2 && kOsc1Level == 3 && kOsc1Coarse == 4 && kOsc1Fine == 5, "Parameter IDs are saved in projects");
static_assert (kOsc2Wave == 6 && kOsc2Level == 7 && kOsc2Coarse == 8 && kOsc2Fine == 9, "Parameter IDs are saved in projects");
static_assert (kFilterMode == 10 && kFilterCutoff == 11 && kFilterResonance == 12 && kFilterKeyFollow == 13 && kFilterEnvAmount == 14, "Parameter IDs are saved in projects");
static_assert (kSpread == 15 && kDryWet == 16 && kVolume == 17, "Parameter IDs are saved in projects");
static_assert (kPartials == 18, "Parameter IDs are saved in projects");

} // namespace

void testClassIds ()
{
    bool foundProcessor = false;
    bool foundController = false;
    const FUID processor (0x3D32BF5A, 0xF1DE45BA, 0x99435755, 0xF45210D3);
    const FUID controller (0x57463240, 0x2F944D73, 0xAB57B18A, 0x2E55CBA6);

    auto* factory = getFactory ();
    for (int32 i = 0; i < factory->countClasses (); ++i)
    {
        PClassInfo info;
        factory->getClassInfo (i, &info);
        foundProcessor = foundProcessor || FUID::fromTUID (info.cid) == processor;
        foundController = foundController || FUID::fromTUID (info.cid) == controller;
    }
    check ("the processor's class ID hasn't changed", foundProcessor);
    check ("the controller's class ID hasn't changed", foundController);
}

void testParameterIds ()
{
    Plug plug;
    check ("there are 19 parameters", plug.controller->getParameterCount () == 19);

    bool allMatch = true;
    for (int32 i = 0; i < plug.controller->getParameterCount (); ++i)
    {
        Vst::ParameterInfo info {};
        plug.controller->getParameterInfo (i, info);
        allMatch = allMatch && info.id == static_cast<Vst::ParamID> (i)
                   && (info.flags & Vst::ParameterInfo::kCanAutomate) != 0;
    }
    check ("IDs follow the enum, and all are automatable", allMatch);
}

void testParameterRanges ()
{
    struct Range
    {
        ParamId id;
        double min;
        double max;
    };
    const Range kRanges[] = {
        { kThreshold, -90, -20 }, { kGlide, 0, 2000 },
        { kOsc1Wave, 0, 2 }, { kOsc1Level, 0, 100 }, { kOsc1Coarse, -24, 24 }, { kOsc1Fine, -100, 100 },
        { kOsc2Wave, 0, 2 }, { kOsc2Level, 0, 100 }, { kOsc2Coarse, -24, 24 }, { kOsc2Fine, -100, 100 },
        { kFilterMode, 0, 3 }, { kFilterCutoff, 20, 20000 }, { kFilterResonance, 0, 100 }, { kFilterKeyFollow, 0, 100 }, { kFilterEnvAmount, -8, 8 },
        { kSpread, 0, 100 }, { kDryWet, 0, 100 }, { kVolume, -60, 6 },
        { kPartials, 5, 24 },
    };

    Plug plug;
    bool allMatch = true;
    for (const auto& range : kRanges)
    {
        allMatch = allMatch && nearly (plug.controller->normalizedParamToPlain (range.id, 0.0), range.min)
                   && nearly (plug.controller->normalizedParamToPlain (range.id, 1.0), range.max);
    }
    check ("every range is as it was", allMatch);

    check ("cutoff is logarithmic", std::abs (plug.controller->normalizedParamToPlain (kFilterCutoff, 0.5) - 632.46) < 0.01);
    check ("glide is squared, and reaches zero", nearly (plug.controller->normalizedParamToPlain (kGlide, 0.5), 500.0) && plug.controller->normalizedParamToPlain (kGlide, 0.0) == 0.0);
    check ("coarse tuning moves in semitones", nearly (plug.controller->normalizedParamToPlain (kOsc1Coarse, 0.51), 0.0));
    check ("partials moves in whole voices, and starts from the five there always were", nearly (plug.controller->normalizedParamToPlain (kPartials, 0.51), 15.0) && nearly (plug.controller->getParamNormalized (kPartials), 0.0));
}

void testSavedFormat ()
{
    Plug plug;
    const auto bytes = plug.getStateBytes ();
    check ("version, count, then 19 ID and value pairs", bytes.size () == 8 + 19 * 12);
    check ("version 1", readAt<int32_t> (bytes, 0) == 1);
    check ("the count is 19", readAt<int32_t> (bytes, 4) == 19);
    check ("the first pair is Threshold", readAt<int32_t> (bytes, 8) == 0 && std::abs (readAt<double> (bytes, 12) - 30.0 / 70.0) < 1e-9);

    MemoryStream empty;
    check ("an empty state is rejected without crashing", plug.component->setState (&empty) != kResultOk);

    // A state from a future version with parameters unknown to this one still loads what it knows.
    MemoryStream stream;
    const int32_t header[2] = { 1, 2 };
    int32 written = 0;
    stream.write (const_cast<int32_t*> (header), sizeof (header), &written);
    const int32_t unknownId = 999;
    const double value = 0.5;
    const int32_t thresholdId = 0;
    stream.write (const_cast<int32_t*> (&unknownId), 4, &written);
    stream.write (const_cast<double*> (&value), 8, &written);
    stream.write (const_cast<int32_t*> (&thresholdId), 4, &written);
    stream.write (const_cast<double*> (&value), 8, &written);
    int64 position = 0;
    stream.seek (0, IBStream::kIBSeekSet, &position);
    check ("an unknown parameter is skipped", plug.component->setState (&stream) == kResultOk);
    check ("and the known one still arrives", nearly (readAt<double> (plug.getStateBytes (), 12), 0.5));
}

void testBuses ()
{
    Plug plug;
    check ("one audio input", plug.component->getBusCount (Vst::kAudio, Vst::kInput) == 1);
    check ("one audio output", plug.component->getBusCount (Vst::kAudio, Vst::kOutput) == 1);
    check ("no event inputs", plug.component->getBusCount (Vst::kEvent, Vst::kInput) == 0);

    Vst::SpeakerArrangement arrangement = 0;
    plug.processor->getBusArrangement (Vst::kInput, 0, arrangement);
    check ("the input is stereo", arrangement == Vst::SpeakerArr::kStereo);
    plug.processor->getBusArrangement (Vst::kOutput, 0, arrangement);
    check ("the output is stereo", arrangement == Vst::SpeakerArr::kStereo);

    FUnknownPtr<IPluginFactory2> factory (getFactory ());
    bool isEffect = false;
    for (int32 i = 0; factory && i < factory->countClasses (); ++i)
    {
        PClassInfo2 info;
        factory->getClassInfo2 (i, &info);
        if (std::strcmp (info.category, kVstAudioEffectClass) == 0)
            isEffect = std::strcmp (info.subCategories, "Fx") == 0;
    }
    check ("it's registered as an effect", isEffect);
}

void testLatencyReport ()
{
    const struct
    {
        double sampleRate;
        uint32 latency;
    } kCases[] = { { 44100.0, 1536 }, { 48000.0, 1536 }, { 88200.0, 3072 }, { 96000.0, 3072 }, { 192000.0, 6144 } };

    for (const auto& testCase : kCases)
    {
        Plug plug (testCase.sampleRate);
        const uint32 latency = plug.processor->getLatencySamples ();
        std::printf ("  %.0f Hz: %u samples, %.1f ms\n", testCase.sampleRate, latency, 1000.0 * latency / testCase.sampleRate);
        check ("the latency is as expected", latency == testCase.latency);
    }
}

} // namespace Tests
} // namespace PartialResynth
