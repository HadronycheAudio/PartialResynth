#include "Controller.h"

#include "Editor.h"

namespace PartialResynth {

Steinberg::IPlugView* Controller::createEditor ()
{
    return new Editor (this);
}

} // namespace PartialResynth
