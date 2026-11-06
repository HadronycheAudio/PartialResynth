// The real editor in an off-screen window, driven with mouse messages on its real controls, while audio
// runs. It doesn't touch the real mouse. Set the environment variable PARTIALRESYNTH_SCREENSHOTS to a folder
// to also save the panel as a BMP file.

#include "Harness.h"
#include "Tests.h"

#include "hadronyche/testhost/Panel.h"

#include "pluginterfaces/gui/iplugview.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Steinberg;

namespace PartialResynth {
namespace Tests {

namespace {

// The centre of the Threshold knob, a little above it, which follows the layout in src/Editor.cpp.
constexpr int kThresholdKnobX = 47;
constexpr int kThresholdKnobY = 64;

void saveScreenshot (HWND window, const ViewRect& size, const char* name)
{
    Hadronyche::TestHost::saveScreenshot (window, size.getWidth (), size.getHeight (), "PARTIALRESYNTH_SCREENSHOTS", name);
}

} // namespace

void testPanel ()
{
    Plug plug;
    IPlugView* view = plug.controller->createView (Vst::ViewType::kEditor);
    check ("the controller creates an editor", view != nullptr);
    if (!view)
        return;

    ViewRect size;
    view->getSize (&size);

    WNDCLASSW windowClass {};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW (nullptr);
    windowClass.lpszClassName = L"PartialResynthPanelTest";
    RegisterClassW (&windowClass);

    HWND window = CreateWindowExW (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, windowClass.lpszClassName, L"", WS_POPUP, -4000, -4000,
                                   size.getWidth (), size.getHeight (), nullptr, nullptr, windowClass.hInstance, nullptr);
    ShowWindow (window, SW_SHOWNOACTIVATE);

    check ("the editor opens", view->attached (window, kPlatformTypeHWND) == kResultOk);
    HWND editorWindow = FindWindowExW (window, nullptr, nullptr, nullptr);
    check ("it has a window", editorWindow != nullptr);

    const auto tone = makeTones ({ { 440.0, 0.5 } }, kBlockSize);

    // In real time rather than as fast as possible, because the panel updates from a timer.
    const auto runWithUi = [&] (double seconds) {
        const ULONGLONG end = GetTickCount64 () + static_cast<ULONGLONG> (seconds * 1000.0);
        while (GetTickCount64 () < end)
        {
            plug.process (tone);
            pump ();
            Sleep (1);
        }
    };

    runWithUi (0.3);
    saveScreenshot (window, size, "panel-default");

    // A knob drag changes its parameter.
    const double before = plug.controller->getParamNormalized (kThreshold);
    // With no host to choose otherwise, knobs follow the mouse's angle around their centre, so this drags
    // to the left of the knob. A knob sweeps 270 degrees from 7:30 to 4:30, so that's 45 degrees in, or 1/6
    // of its range, and a little more because the mouse is slightly above the knob's centre.
    SendMessageW (editorWindow, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM (kThresholdKnobX, kThresholdKnobY));
    pump ();
    for (int i = 1; i <= 20; ++i)
    {
        SendMessageW (editorWindow, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM (kThresholdKnobX - i * 5, kThresholdKnobY));
        pump ();
    }
    SendMessageW (editorWindow, WM_LBUTTONUP, 0, MAKELPARAM (kThresholdKnobX - 100, kThresholdKnobY));
    pump ();
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
    saveScreenshot (window, size, "panel-edited");

    check ("the editor closes", view->removed () == kResultOk);
    view->release ();
    pump ();
    DestroyWindow (window);
    UnregisterClassW (windowClass.lpszClassName, windowClass.hInstance);
}

} // namespace Tests
} // namespace PartialResynth
