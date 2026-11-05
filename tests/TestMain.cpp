// Usage: PartialResynthTests <path to the plug-in's DLL> [test name]
//        PartialResynthTests --list
//
// With no test name, everything runs except "cpu", which only reports a measurement.

#include "Harness.h"
#include "Tests.h"

#include <cstdio>
#include <cwchar>

using namespace PartialResynth::Tests;

namespace {

struct Test
{
    const wchar_t* name;
    void (*function) ();
};

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

    { L"panel", testPanel },
};

} // namespace

int wmain (int argc, wchar_t** argv)
{
    if (argc >= 2 && std::wcscmp (argv[1], L"--list") == 0)
    {
        for (const auto& test : kTests)
            std::wprintf (L"%ls\n", test.name);
        std::wprintf (L"cpu\n");
        return 0;
    }

    if (argc < 2)
    {
        std::printf ("Usage: PartialResynthTests <path to the plug-in's DLL> [test name]\n");
        return 2;
    }

    if (!loadPlugin (argv[1]))
    {
        std::wprintf (L"Could not load the plug-in: %ls\n", argv[1]);
        return 2;
    }

    const wchar_t* filter = argc >= 3 ? argv[2] : nullptr;
    bool foundTest = false;

    if (filter && std::wcscmp (filter, L"cpu") == 0)
    {
        foundTest = true;
        measureCpu ();
    }

    for (const auto& test : kTests)
    {
        if (filter && std::wcscmp (filter, test.name) != 0)
            continue;

        foundTest = true;
        std::wprintf (L"[%ls]\n", test.name);
        std::fflush (stdout);
        test.function ();
    }

    unloadPlugin ();

    if (!foundTest)
    {
        std::wprintf (L"No test named %ls. Use --list to see them.\n", filter);
        return 2;
    }

    std::printf ("\n%d failure(s)\n", getFailureCount ());
    return getFailureCount () == 0 ? 0 : 1;
}
