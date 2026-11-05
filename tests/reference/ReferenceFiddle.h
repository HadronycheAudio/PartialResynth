#pragma once

// The original fiddle~ from Pure Data, unmodified, behind a small interface for the tests to compare with.

#ifdef __cplusplus
extern "C" {
#endif

// As "fiddle~ <windowSize> 1 100 100", then "amp-range <powerFloor> <powerFloor + 10>".
void* referenceFiddleCreate (int windowSize, float sampleRate, float powerFloor);
void referenceFiddleDestroy (void* fiddle);

// Takes a block of samples that fits the hop. Returns 1 when that completes a frame.
int referenceFiddleProcess (void* fiddle, float* samples, int count);

// The peak output, up to 100 places. Those beyond the last peak found have an amplitude of zero.
void referenceFiddleGetPeak (void* fiddle, int index, float* frequency, float* amplitude);
float referenceFiddleGetTotalPower (void* fiddle);

#ifdef __cplusplus
}
#endif
