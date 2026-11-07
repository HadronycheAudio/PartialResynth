#include "Controller.h"

#include "Editor.h"
#include "Parameters.h"

namespace PartialResynth {

Controller::Controller () : SpecController (getParamSpec, kParamCount, readParamState)
{
}

Steinberg::IPlugView* Controller::createEditor ()
{
    return new Editor (this);
}

} // namespace PartialResynth
