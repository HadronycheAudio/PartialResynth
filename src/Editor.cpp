#include "Editor.h"

#include "Controller.h"
#include "ParamIds.h"

#include "hadronyche/vst/RustPlugin.h"

#include "vstgui/vstgui.h"

using namespace VSTGUI;

namespace PartialResynth {

namespace {

constexpr Hadronyche::PanelLayout kLayout {};

const Hadronyche::PanelTheme kTheme {
    CColor (26, 32, 36),
    CColor (40, 49, 54),
    CColor (68, 82, 90),
    CColor (92, 190, 176),
    CColor (225, 230, 232),
    CColor (158, 170, 175),
};

} // namespace

Editor::Editor (Controller* controller)
: PanelEditor (controller, kLayout.rowWidth ({ 3, 4, 4, 5, 3 }), kLayout.rowHeight (), Hadronyche::RustPlugin::getSpec, kParamCount, kTheme, kLayout)
{
}

void Editor::buildPanel ()
{
    const int knobY = kLayout.knobRow ();

    {
        const int left = addColumnSection (3, "Analysis");
        addKnob (left, knobY, kThreshold, "Threshold");
        addKnob (left + kLayout.columnWidth, knobY, kPartials, "Partials");
        addKnob (left + 2 * kLayout.columnWidth, knobY, kGlide, "Glide");
    }

    const auto addOscillator = [&] (const char* title, ParamId wave, ParamId level, ParamId coarse, ParamId fine) {
        const int left = addColumnSection (4, title);
        addMenu (left, knobY, wave, "Wave");
        addKnob (left + kLayout.columnWidth, knobY, level, "Level");
        addKnob (left + 2 * kLayout.columnWidth, knobY, coarse, "Coarse", true);
        addKnob (left + 3 * kLayout.columnWidth, knobY, fine, "Fine", true);
    };
    addOscillator ("Oscillator 1", kOsc1Wave, kOsc1Level, kOsc1Coarse, kOsc1Fine);
    addOscillator ("Oscillator 2", kOsc2Wave, kOsc2Level, kOsc2Coarse, kOsc2Fine);

    {
        const int left = addColumnSection (5, "Filter");
        addMenu (left, knobY, kFilterMode, "Mode");
        addKnob (left + kLayout.columnWidth, knobY, kFilterCutoff, "Cutoff");
        addKnob (left + 2 * kLayout.columnWidth, knobY, kFilterResonance, "Resonance");
        addKnob (left + 3 * kLayout.columnWidth, knobY, kFilterKeyFollow, "Key follow");
        addKnob (left + 4 * kLayout.columnWidth, knobY, kFilterEnvAmount, "Env amount", true);
    }
    {
        const int left = addColumnSection (3, "Output");
        addKnob (left, knobY, kSpread, "Spread");
        addKnob (left + kLayout.columnWidth, knobY, kDryWet, "Dry/wet");
        addKnob (left + 2 * kLayout.columnWidth, knobY, kVolume, "Volume");
    }
}

} // namespace PartialResynth
