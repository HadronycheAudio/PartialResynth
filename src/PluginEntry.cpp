// VST3 module entry: registers the processor and controller classes with the host.

#include "Controller.h"
#include "PluginConfig.h"
#include "Processor.h"

#include "public.sdk/source/main/pluginfactory.h"

BEGIN_FACTORY_DEF (PARTIALRESYNTH_VENDOR, PARTIALRESYNTH_URL, PARTIALRESYNTH_EMAIL)

    DEF_CLASS2 (INLINE_UID_FROM_FUID (PartialResynth::Processor::getClassFUID ()),
                PClassInfo::kManyInstances,
                kVstAudioEffectClass,
                PARTIALRESYNTH_NAME,
                Vst::kDistributable,
                Vst::PlugType::kFx,
                PARTIALRESYNTH_VERSION,
                kVstVersionString,
                PartialResynth::Processor::createInstance)

    DEF_CLASS2 (INLINE_UID_FROM_FUID (PartialResynth::Controller::getClassFUID ()),
                PClassInfo::kManyInstances,
                kVstComponentControllerClass,
                PARTIALRESYNTH_NAME,
                0,
                "",
                PARTIALRESYNTH_VERSION,
                kVstVersionString,
                PartialResynth::Controller::createInstance)

END_FACTORY
