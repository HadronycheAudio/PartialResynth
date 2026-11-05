#pragma once

#include <array>
#include <vector>

namespace PartialResynth {

// A frame's sinusoidal peak: the same frequency and amplitude as fiddle~'s peak output.
struct SpectralPeak
{
    float frequency; // In Hz.
    float amplitude; // Linear: a full-scale sine reads about 1.
};

// The peak analysis of fiddle~ from Pure Data, by Miller Puckette and Ted Apel, ported to C++. The pitch
// tracking and note detection aren't here, because nothing here uses them.
//
// Like the original, it takes half a window of new samples per frame, and works out the spectrum of the
// whole window from that half's FFT and the previous half's. The arithmetic deliberately stays as close to
// the original as it can, down to the rounded constants: tests/FiddleTests.cpp runs the original source
// alongside this and compares every frame, so it isn't "tidied" independently of that.
//
// It differs from the original in two ways:
// - It finds peaks up to kMaxPeaks and returns all of them, and the caller chooses. The original outputs the
//   lowest ones in frequency, and stops looking once it has what it was asked for.
// - Below the power threshold, the frame has no peaks. The original skips updating its peak list here, so it
//   goes on reporting the last peaks it found for as long as the input stays quiet.
class FiddleAnalyzer
{
public:
    static constexpr int kMaxPeaks = 100;

    // Allocates, so this is only for outside the audio thread. windowSize is fiddle~'s "npoints", a power of
    // two: a frame arrives every windowSize / 2 samples, the first after that many minus startOffset.
    void prepare (int windowSize, double sampleRate, int startOffset = 0);

    // In fiddle~'s scale, its "amp-range" low value, which it compares against total power.
    void setPowerFloor (float powerFloor) { _powerFloor = powerFloor; }

    // On fiddle~'s scale, what total power a full-scale sine gives for a window size, about 101 for 2048.
    // Subtracting this converts that scale to dB below a full-scale sine.
    static float getFullScalePower (int windowSize);

    // Returns true when this sample completes a frame, which is then in getPeaks.
    bool push (float sample)
    {
        _input[static_cast<size_t> (_phase)] = sample;
        if (++_phase < _hop)
            return false;

        _phase = 0;
        analyze ();
        return true;
    }

    // Moves on by count samples of silence, for an analyzer that has already taken in at least a window of
    // silence. Its buffers can only hold zeros at that point, so this is the same as pushing those zeros.
    void skipSilence (int count) { _phase = (_phase + count) % _hop; }

    // In ascending frequency, the order they're found in.
    const SpectralPeak* getPeaks () const { return _peaks.data (); }
    int getPeakCount () const { return _peakCount; }

    float getTotalPower () const { return _totalPower; } // fiddle~'s scale, as its envelope outlet gives it.

private:
    void analyze ();
    void fft (float* data) const;

    int _hop { 0 };
    int _phase { 0 };
    double _sampleRate { 44100.0 };
    float _powerFloor { 40.0f };

    std::vector<float> _input;
    std::vector<float> _lastAnalysis;
    std::vector<float> _spiral;
    std::vector<float> _spectrum1;
    std::vector<float> _spectrum2;
    std::vector<float> _twiddles;
    std::vector<int> _bitReversed;

    std::array<SpectralPeak, kMaxPeaks> _peaks {};
    int _peakCount { 0 };
    float _totalPower { 0.0f };
};

} // namespace PartialResynth
