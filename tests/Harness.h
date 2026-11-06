#pragma once

// The test host from HadronycheCore, and what this plug-in's tests add to it.

#include "Parameters.h"

#include "hadronyche/testhost/Host.h"

#include <cstddef>
#include <vector>

namespace PartialResynth {
namespace Tests {

using namespace Hadronyche::TestHost;

// What the plug-in should report at 44.1 and 48 kHz. Half the window, plus a quarter window for the voices
// to reach what each frame found.
constexpr size_t kLatency = 1536;

using Plug = PlugBase;

double measureFrequency (const Samples& samples, double around, double sampleRate = kSampleRate);

} // namespace Tests
} // namespace PartialResynth
