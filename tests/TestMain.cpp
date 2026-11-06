// Usage: PartialResynthTests <path to the plug-in's DLL> [test name]
//        PartialResynthTests --list
//
// With no test name, everything runs except "cpu", which only reports a measurement.

#include "Harness.h"
#include "Tests.h"

#include "hadronyche/testhost/Runner.h"

#include <iterator>

using namespace PartialResynth::Tests;

namespace {

const Test kTests[] = {
    { L"class-ids", testClassIds },
    { L"parameter-ids", testParameterIds },
    { L"parameter-ranges", testParameterRanges },
    { L"saved-format", testSavedFormat },
    { L"buses", testBuses },
    { L"latency-report", testLatencyReport },

    { L"fiddle-match", testFiddleMatch },
    { L"fiddle-accuracy", testFiddleAccuracy },
    { L"fiddle-floor", testFiddleFloor },
    { L"fiddle-power", testFiddlePower },
    { L"fiddle-sample-rates", testFiddleSampleRates },
    { L"tracker-loudest", testTrackerLoudest },
    { L"tracker-continuity", testTrackerContinuity },
    { L"tracker-fade", testTrackerFade },
    { L"tracker-new-waits", testTrackerNewWaits },
    { L"tracker-keeps-place", testTrackerKeepsPlace },

    { L"resynth-partials", testResynthPartials },
    { L"resynth-loudest", testResynthLoudest },
    { L"resynth-continuous", testResynthContinuous },
    { L"resynth-follows", testResynthFollows },
    { L"resynth-silence", testResynthSilence },
    { L"resynth-threshold", testResynthThreshold },
    { L"latency-alignment", testLatencyAlignment },

    { L"osc-waves", testOscWaves },
    { L"osc-tuning", testOscTuning },
    { L"osc-levels", testOscLevels },
    { L"osc-aliasing", testOscAliasing },
    { L"filter-modes", testFilterModes },
    { L"filter-resonance", testFilterResonance },
    { L"key-follow", testKeyFollow },
    { L"env-to-cutoff", testEnvToCutoff },
    { L"glide", testGlide },
    { L"spread", testSpread },
    { L"dry-wet", testDryWet },
    { L"volume", testVolume },
    { L"block-sizes", testBlockSizes },
    { L"determinism", testDeterminism },
    { L"extreme-settings", testExtremeSettings },
    { L"parameter-state", testParameterState },
    { L"parameter-text", testParameterText },

#if defined(_WIN32)
    { L"panel", testPanel },
#endif
};

} // namespace

HADRONYCHE_TEST_MAIN
{
    return runTests (argc, argv, { "PartialResynthTests", kTests, std::size (kTests), measureCpu });
}
