#pragma once

#include "hadronyche/gui/PanelEditor.h"

namespace PartialResynth {

class Controller;

// The plug-in window: one row of controls, in signal path order. They all apply to all five voices.
class Editor : public Hadronyche::PanelEditor
{
public:
    explicit Editor (Controller* controller);

private:
    void buildPanel () override;
};

} // namespace PartialResynth
