#pragma once

// A minimal VST3 host. It loads the built plug-in and drives it through the same interfaces a real host
// uses, so the tests exercise the real binary rather than individual classes.

#include "Parameters.h"

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <cstddef>
#include <vector>

namespace PartialResynth {
namespace Tests {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 256;

// What the plug-in should report at 44.1 and 48 kHz. Half the window, plus a quarter window for the voices
// to reach what each frame found.
constexpr size_t kLatency = 1536;

using Samples = std::vector<float>;

struct Tone
{
    double frequency;
    double amplitude;
};

struct Stereo
{
    Samples left;
    Samples right;
    std::vector<bool> blockSilent; // The silence flags the plug-in set, per block.
};

// Loading the plug-in.
bool loadPlugin (const wchar_t* path);
void unloadPlugin ();
Steinberg::IPluginFactory* getFactory ();

// Results. Measurements print as they go, so that a failure comes with its numbers.
void check (const char* what, bool ok);
int getFailureCount ();

class ParameterChanges;

// One instance of the plug-in, connected and active, as a host would set it up.
class Plug
{
public:
    explicit Plug (double sampleRate = kSampleRate, int maxBlockSize = kBlockSize);
    ~Plug ();

    Plug (const Plug&) = delete;
    Plug& operator= (const Plug&) = delete;

    // Applied to both sides, as a host would forward it, and takes effect in the next block.
    void set (ParamId id, double plain);

    // Runs the input through in blocks, on both channels unless it's given one for each.
    Stereo process (const Samples& mono, int blockSize = kBlockSize);
    Stereo process (const Samples& left, const Samples& right, int blockSize = kBlockSize);

    std::vector<char> getStateBytes ();

    Steinberg::Vst::IComponent* component { nullptr };
    Steinberg::Vst::IAudioProcessor* processor { nullptr };
    Steinberg::Vst::IEditController* controller { nullptr };

private:
    ParameterChanges* _changes;
    Steinberg::Vst::IConnectionPoint* _componentConnection { nullptr };
    Steinberg::Vst::IConnectionPoint* _controllerConnection { nullptr };
};

// Test signals.
Samples makeTones (const std::vector<Tone>& tones, size_t count, double sampleRate = kSampleRate);
Samples makeNoise (double amplitude, size_t count);
Samples concatenate (const Samples& first, const Samples& second);
Samples add (const Samples& first, const Samples& second);

// Measurements.
Samples slice (const Samples& samples, size_t from, size_t count);
double rms (const Samples& samples, size_t from = 0);
double peak (const Samples& samples, size_t from = 0);
double db (double value);
double toneLevel (const Samples& samples, double hz, size_t from = 0, double sampleRate = kSampleRate); // Hann windowed.
double measureFrequency (const Samples& samples, double around, double sampleRate = kSampleRate);
bool isFinite (const Samples& samples);

} // namespace Tests
} // namespace PartialResynth
