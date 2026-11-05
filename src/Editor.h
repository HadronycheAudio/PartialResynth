#pragma once

#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/vstguibase.h"

#include <vector>

namespace VSTGUI {
class CControl;
class CTextLabel;
class CVSTGUITimer;
} // namespace VSTGUI

namespace PartialResynth {

class Controller;

// The plug-in window: one row of controls, in signal path order. They all apply to all five voices.
class Editor : public Steinberg::Vst::VSTGUIEditor, public VSTGUI::IControlListener
{
public:
    explicit Editor (Controller* controller);

    bool PLUGIN_API open (void* parent, const VSTGUI::PlatformType& platformType) SMTG_OVERRIDE;
    void PLUGIN_API close () SMTG_OVERRIDE;

    void valueChanged (VSTGUI::CControl* control) SMTG_OVERRIDE;
    void controlBeginEdit (VSTGUI::CControl* control) SMTG_OVERRIDE;
    void controlEndEdit (VSTGUI::CControl* control) SMTG_OVERRIDE;

private:
    struct Binding
    {
        VSTGUI::CControl* control;
        VSTGUI::CTextLabel* valueLabel; // Null for controls that show their value themselves.
    };

    void addKnob (int x, int y, Steinberg::Vst::ParamID id, const char* title, bool bipolar = false);
    void addMenu (int x, int y, Steinberg::Vst::ParamID id, const char* title);
    void syncControls ();

    Controller* _controller;
    std::vector<Binding> _bindings;

    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> _syncTimer;
};

} // namespace PartialResynth
