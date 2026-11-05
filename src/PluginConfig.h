#pragma once

// Identity of the plug-in. Keep the FUIDs as they are: hosts use them to find the plug-in and restore
// saved projects.

#define PARTIALRESYNTH_NAME "PartialResynth"
#define PARTIALRESYNTH_VENDOR "Hadronyche"
#define PARTIALRESYNTH_URL ""
#define PARTIALRESYNTH_EMAIL ""

// Class IDs, passed to Steinberg::FUID (a, b, c, d)
#define PARTIALRESYNTH_PROCESSOR_UID 0x3D32BF5A, 0xF1DE45BA, 0x99435755, 0xF45210D3
#define PARTIALRESYNTH_CONTROLLER_UID 0x57463240, 0x2F944D73, 0xAB57B18A, 0x2E55CBA6
