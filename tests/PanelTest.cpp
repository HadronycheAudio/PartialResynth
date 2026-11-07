// The real editor in an off-screen window, driven with mouse messages on its real controls, while audio
// runs. It doesn't touch the real mouse. Set the environment variable PARTIALRESYNTH_SCREENSHOTS to a folder
// to also save the panel as a BMP file.

#include "Harness.h"
#include "Tests.h"

#include "hadronyche/testhost/Panel.h"

#include <cmath>
#include <cstdio>

namespace PartialResynth {
namespace Tests {

namespace {

// The centre of the Threshold knob, a little above it, which follows the layout in src/Editor.cpp.
constexpr int kThresholdKnobX = 47;
constexpr int kThresholdKnobY = 64;

} // namespace

void testPanel ()
{
    Plug plug;
    PanelWindow panel (plug.controller, L"PartialResynthPanelTest");
    check ("the controller creates an editor", panel.view != nullptr);
    if (!panel.view)
        return;

    check ("the editor opens", panel.open ());
    check ("it has a window", panel.editorWindow != nullptr);

    const auto tone = makeTones ({ { 440.0, 0.5 } }, kBlockSize);
    const auto runWithUi = [&] (double seconds) { panel.runFor (seconds, [&] { plug.process (tone); }); };

    runWithUi (0.3);
    panel.saveScreenshot ("PARTIALRESYNTH_SCREENSHOTS", "panel-default");

    // A knob drag changes its parameter. The drag ends 1/6 of the way in, and a little more because the mouse is
    // slightly above the knob's centre.
    const double before = plug.controller->getParamNormalized (kThreshold);
    panel.dragLeft (kThresholdKnobX, kThresholdKnobY);
    const double after = plug.controller->getParamNormalized (kThreshold);
    std::printf ("  Threshold went from %.3f to %.3f\n", before, after);
    check ("dragging the Threshold knob moves Threshold to where the mouse is", std::abs (after - 0.184) < 0.02);

    // Automation reaches the panel: the controls aren't the source of these changes.
    plug.set (kOsc1Wave, 1);
    plug.set (kOsc2Wave, 2);
    plug.set (kOsc2Level, 60);
    plug.set (kOsc2Coarse, 7);
    plug.set (kFilterMode, 2);
    plug.set (kFilterCutoff, 1500);
    plug.set (kFilterEnvAmount, -3);
    plug.set (kVolume, -12.0);
    runWithUi (0.3);
    panel.saveScreenshot ("PARTIALRESYNTH_SCREENSHOTS", "panel-edited");

    check ("the editor closes", panel.close ());
}

} // namespace Tests
} // namespace PartialResynth
