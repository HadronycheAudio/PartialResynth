#include "Processor.h"

#include "Controller.h"

namespace PartialResynth {

Processor::Processor () : RustProcessor (Controller::getClassFUID ())
{
}

} // namespace PartialResynth
