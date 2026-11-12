#pragma once

namespace PartialResynth {
namespace Tests {

// CompatibilityTests.cpp
void testClassIds ();
void testParameterIds ();
void testParameterRanges ();
void testSavedFormat ();
void testBuses ();
void testLatencyReport ();

// ResynthTests.cpp
void testResynthPartials ();
void testResynthLoudest ();
void testResynthContinuous ();
void testResynthFollows ();
void testResynthSilence ();
void testResynthThreshold ();
void testResynthCount ();
void testLatencyAlignment ();

// SynthTests.cpp
void testOscWaves ();
void testOscTuning ();
void testOscLevels ();
void testOscAliasing ();
void testFilterModes ();
void testFilterResonance ();
void testKeyFollow ();
void testEnvToCutoff ();
void testGlide ();
void testSpread ();
void testDryWet ();
void testVolume ();
void testBlockSizes ();
void testDeterminism ();
void testExtremeSettings ();
void testParameterState ();
void testParameterText ();
void measureCpu ();

// PanelTest.cpp
void testPanel ();

} // namespace Tests
} // namespace PartialResynth
