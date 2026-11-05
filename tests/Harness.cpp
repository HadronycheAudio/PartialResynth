#include "Harness.h"

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "public.sdk/source/common/memorystream.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace Steinberg;

DEF_CLASS_IID (Vst::IEditController)
DEF_CLASS_IID (Vst::IComponent)
DEF_CLASS_IID (Vst::IAudioProcessor)
DEF_CLASS_IID (Vst::IConnectionPoint)
DEF_CLASS_IID (Vst::IMessage)
DEF_CLASS_IID (Vst::IAttributeList)
DEF_CLASS_IID (Vst::IHostApplication)
DEF_CLASS_IID (IPlugView)

namespace PartialResynth {
namespace Tests {

namespace {

constexpr double kPi = 3.14159265358979;

#define TESTS_NO_REFCOUNT                                                                     \
    tresult PLUGIN_API queryInterface (const TUID, void**) override { return kNoInterface; }  \
    uint32 PLUGIN_API addRef () override { return 1; }                                        \
    uint32 PLUGIN_API release () override { return 1; }

class Host : public Vst::IHostApplication
{
public:
    tresult PLUGIN_API queryInterface (const TUID iid, void** object) override
    {
        if (FUnknownPrivate::iidEqual (iid, Vst::IHostApplication::iid) || FUnknownPrivate::iidEqual (iid, FUnknown::iid))
        {
            *object = static_cast<Vst::IHostApplication*> (this);
            return kResultOk;
        }
        *object = nullptr;
        return kNoInterface;
    }

    uint32 PLUGIN_API addRef () override { return 1; }
    uint32 PLUGIN_API release () override { return 1; }

    tresult PLUGIN_API getName (Vst::String128 name) override
    {
        name[0] = 0;
        return kResultOk;
    }

    tresult PLUGIN_API createInstance (TUID, TUID, void** object) override
    {
        *object = nullptr;
        return kResultFalse;
    }
};

class ValueQueue : public Vst::IParamValueQueue
{
public:
    TESTS_NO_REFCOUNT

    ValueQueue (Vst::ParamID id, double value) : _id (id), _value (value) {}

    Vst::ParamID PLUGIN_API getParameterId () override { return _id; }
    int32 PLUGIN_API getPointCount () override { return 1; }

    tresult PLUGIN_API getPoint (int32, int32& offset, Vst::ParamValue& value) override
    {
        offset = 0;
        value = _value;
        return kResultTrue;
    }

    tresult PLUGIN_API addPoint (int32, Vst::ParamValue, int32&) override { return kResultFalse; }

private:
    Vst::ParamID _id;
    double _value;
};

Host gHost;
HMODULE gModule = nullptr;
IPluginFactory* gFactory = nullptr;
bool (*gExitDll) () = nullptr;
TUID gComponentId;
TUID gControllerId;
int gFailures = 0;

} // namespace

class ParameterChanges : public Vst::IParameterChanges
{
public:
    TESTS_NO_REFCOUNT

    int32 PLUGIN_API getParameterCount () override { return static_cast<int32> (queues.size ()); }
    Vst::IParamValueQueue* PLUGIN_API getParameterData (int32 index) override { return &queues[static_cast<size_t> (index)]; }
    Vst::IParamValueQueue* PLUGIN_API addParameterData (const Vst::ParamID&, int32&) override { return nullptr; }

    std::vector<ValueQueue> queues;
};

bool loadPlugin (const wchar_t* path)
{
    gModule = LoadLibraryW (path);
    if (!gModule)
        return false;

    auto initDll = reinterpret_cast<bool (*) ()> (GetProcAddress (gModule, "InitDll"));
    gExitDll = reinterpret_cast<bool (*) ()> (GetProcAddress (gModule, "ExitDll"));
    auto getPluginFactory = reinterpret_cast<GetFactoryProc> (GetProcAddress (gModule, "GetPluginFactory"));
    if (!initDll || !gExitDll || !getPluginFactory || !initDll ())
        return false;

    gFactory = getPluginFactory ();
    for (int32 i = 0; i < gFactory->countClasses (); ++i)
    {
        PClassInfo info;
        gFactory->getClassInfo (i, &info);
        if (std::strcmp (info.category, kVstAudioEffectClass) == 0)
            std::memcpy (gComponentId, info.cid, sizeof (TUID));
        else if (std::strcmp (info.category, kVstComponentControllerClass) == 0)
            std::memcpy (gControllerId, info.cid, sizeof (TUID));
    }
    return true;
}

void unloadPlugin ()
{
    gFactory->release ();
    gExitDll ();
    FreeLibrary (gModule);
}

IPluginFactory* getFactory ()
{
    return gFactory;
}

void check (const char* what, bool ok)
{
    std::printf ("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++gFailures;
}

int getFailureCount ()
{
    return gFailures;
}

Plug::Plug (double sampleRate, int maxBlockSize) : _changes (new ParameterChanges)
{
    gFactory->createInstance (gComponentId, Vst::IComponent::iid, reinterpret_cast<void**> (&component));
    component->initialize (&gHost);
    component->queryInterface (Vst::IAudioProcessor::iid, reinterpret_cast<void**> (&processor));

    gFactory->createInstance (gControllerId, Vst::IEditController::iid, reinterpret_cast<void**> (&controller));
    controller->initialize (&gHost);

    component->queryInterface (Vst::IConnectionPoint::iid, reinterpret_cast<void**> (&_componentConnection));
    controller->queryInterface (Vst::IConnectionPoint::iid, reinterpret_cast<void**> (&_controllerConnection));
    _componentConnection->connect (_controllerConnection);
    _controllerConnection->connect (_componentConnection);

    Vst::ProcessSetup setup { Vst::kRealtime, Vst::kSample32, maxBlockSize, sampleRate };
    processor->setupProcessing (setup);
    component->activateBus (Vst::kAudio, Vst::kInput, 0, true);
    component->activateBus (Vst::kAudio, Vst::kOutput, 0, true);
    component->setActive (true);
    processor->setProcessing (true);
}

Plug::~Plug ()
{
    processor->setProcessing (false);
    component->setActive (false);
    _componentConnection->disconnect (_controllerConnection);
    _controllerConnection->disconnect (_componentConnection);
    _componentConnection->release ();
    _controllerConnection->release ();
    controller->terminate ();
    controller->release ();
    processor->release ();
    component->terminate ();
    component->release ();
    delete _changes;
}

void Plug::set (ParamId id, double plain)
{
    const double normalized = controller->plainParamToNormalized (id, plain);
    controller->setParamNormalized (id, normalized);
    _changes->queues.emplace_back (id, normalized);
}

Stereo Plug::process (const Samples& mono, int blockSize)
{
    return process (mono, mono, blockSize);
}

Stereo Plug::process (const Samples& left, const Samples& right, int blockSize)
{
    const size_t total = left.size ();

    Stereo result;
    result.left.resize (total);
    result.right.resize (total);

    Samples inputLeft = left;
    Samples inputRight = right;

    for (size_t position = 0; position < total; position += static_cast<size_t> (blockSize))
    {
        const int count = static_cast<int> (std::min<size_t> (static_cast<size_t> (blockSize), total - position));
        float* inputs[2] = { &inputLeft[position], &inputRight[position] };
        float* outputs[2] = { &result.left[position], &result.right[position] };

        Vst::AudioBusBuffers inputBus {};
        inputBus.numChannels = 2;
        inputBus.channelBuffers32 = inputs;

        Vst::AudioBusBuffers outputBus {};
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = outputs;

        Vst::ProcessData data {};
        data.processMode = Vst::kRealtime;
        data.symbolicSampleSize = Vst::kSample32;
        data.numSamples = count;
        data.numInputs = 1;
        data.inputs = &inputBus;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.inputParameterChanges = _changes->queues.empty () ? nullptr : _changes;
        processor->process (data);
        _changes->queues.clear ();

        result.blockSilent.push_back (outputBus.silenceFlags == 3);
    }
    return result;
}

std::vector<char> Plug::getStateBytes ()
{
    MemoryStream stream;
    component->getState (&stream);
    return std::vector<char> (stream.getData (), stream.getData () + stream.getSize ());
}

Samples makeTones (const std::vector<Tone>& tones, size_t count, double sampleRate)
{
    Samples samples (count, 0.0f);
    for (const auto& tone : tones)
    {
        for (size_t i = 0; i < count; ++i)
            samples[i] += static_cast<float> (tone.amplitude * std::sin (2.0 * kPi * tone.frequency * static_cast<double> (i) / sampleRate));
    }
    return samples;
}

Samples makeNoise (double amplitude, size_t count)
{
    Samples samples (count);
    uint32_t random = 0x9E3779B9u;
    for (auto& sample : samples)
    {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        sample = static_cast<float> (amplitude * (random / 2147483648.0 - 1.0));
    }
    return samples;
}

Samples concatenate (const Samples& first, const Samples& second)
{
    Samples result = first;
    result.insert (result.end (), second.begin (), second.end ());
    return result;
}

Samples add (const Samples& first, const Samples& second)
{
    Samples result = first;
    for (size_t i = 0; i < result.size () && i < second.size (); ++i)
        result[i] += second[i];
    return result;
}

Samples slice (const Samples& samples, size_t from, size_t count)
{
    return Samples (samples.begin () + static_cast<std::ptrdiff_t> (from),
                    samples.begin () + static_cast<std::ptrdiff_t> (std::min (samples.size (), from + count)));
}

double rms (const Samples& samples, size_t from)
{
    double energy = 0.0;
    for (size_t i = from; i < samples.size (); ++i)
        energy += static_cast<double> (samples[i]) * samples[i];
    return std::sqrt (energy / static_cast<double> (std::max<size_t> (1, samples.size () - from)));
}

double peak (const Samples& samples, size_t from)
{
    double highest = 0.0;
    for (size_t i = from; i < samples.size (); ++i)
        highest = std::max (highest, static_cast<double> (std::abs (samples[i])));
    return highest;
}

double db (double value)
{
    return 20.0 * std::log10 (std::max (value, 1e-12));
}

double toneLevel (const Samples& samples, double hz, size_t from, double sampleRate)
{
    const size_t count = samples.size () - from;
    double real = 0.0;
    double imaginary = 0.0;
    double windowSum = 0.0;
    for (size_t i = 0; i < count; ++i)
    {
        const double window = 0.5 - 0.5 * std::cos (2.0 * kPi * static_cast<double> (i) / static_cast<double> (count - 1));
        const double phase = 2.0 * kPi * hz * static_cast<double> (i) / sampleRate;
        real += window * samples[from + i] * std::cos (phase);
        imaginary += window * samples[from + i] * std::sin (phase);
        windowSum += window;
    }
    return 2.0 * std::sqrt (real * real + imaginary * imaginary) / windowSum;
}

// Scans around the expected frequency for the strongest line, in fine steps. Other tones in the signal don't
// disturb it, as they would an autocorrelation.
double measureFrequency (const Samples& samples, double around, double sampleRate)
{
    double best = around;
    double bestLevel = -1.0;
    for (double hz = around * 0.9; hz <= around * 1.1; hz += around * 0.0005)
    {
        const double level = toneLevel (samples, hz, 0, sampleRate);
        if (level > bestLevel)
        {
            bestLevel = level;
            best = hz;
        }
    }
    return best;
}

bool isFinite (const Samples& samples)
{
    return std::all_of (samples.begin (), samples.end (), [] (float sample) { return std::isfinite (sample); });
}

} // namespace Tests
} // namespace PartialResynth
