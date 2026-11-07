// What the LV2 wrapper and its Turtle need to know about this plug-in. The URI and the symbols are saved in hosts'
// projects, so never change them, and a new parameter adds a symbol at the end.

#include "Parameters.h"

#include "hadronyche/lv2/PluginInfo.h"

namespace {

const char* const kSymbols[] = {
    "threshold",
    "glide",
    "osc1_wave",
    "osc1_level",
    "osc1_coarse",
    "osc1_fine",
    "osc2_wave",
    "osc2_level",
    "osc2_coarse",
    "osc2_fine",
    "filter_mode",
    "filter_cutoff",
    "filter_resonance",
    "filter_key_follow",
    "filter_env_amount",
    "spread",
    "dry_wet",
    "volume",
};

static_assert (sizeof (kSymbols) / sizeof (kSymbols[0]) == PartialResynth::kParamCount, "One symbol for each parameter");

} // namespace

const Hadronyche::Lv2::PluginInfo& Hadronyche::Lv2::getPluginInfo ()
{
    static const PluginInfo info {
        "https://github.com/HadronycheAudio/PartialResynth",
        "PartialResynth",
        "SpectralPlugin",
        PartialResynth::getParamSpec,
        PartialResynth::kParamCount,
        kSymbols,
        2, // LV2 treats an odd minor version as a development build.
        0,
        true, // Audio input
        false, // MIDI
        false, // Transport
        true, // Latency
    };
    return info;
}
