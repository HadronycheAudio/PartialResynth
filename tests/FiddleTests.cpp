// The analysis, compared with Pure Data's original fiddle~, and the tracker that chooses what the voices play.

#include "Harness.h"
#include "Tests.h"

#include "ReferenceFiddle.h"
#include "dsp/FiddleAnalyzer.h"
#include "dsp/PartialTracker.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace PartialResynth {
namespace Tests {

namespace {

constexpr int kWindow = 2048;
constexpr int kHop = kWindow / 2;
constexpr int kReferenceBlock = 64;

// Runs a signal through the original and the port, and reports the worst difference across every frame.
void compareWithOriginal (const char* name, const Samples& signal, int window = kWindow, double sampleRate = kSampleRate, float powerFloor = 40.0f)
{
    void* reference = referenceFiddleCreate (window, static_cast<float> (sampleRate), powerFloor);

    FiddleAnalyzer analyzer;
    analyzer.prepare (window, sampleRate);
    analyzer.setPowerFloor (powerFloor);

    int frames = 0;
    int countMismatches = 0;
    int peaksCompared = 0;
    double worstFrequency = 0.0;
    double worstAmplitude = 0.0;
    double worstPower = 0.0;

    Samples block (kReferenceBlock);
    for (size_t position = 0; position + kReferenceBlock <= signal.size (); position += kReferenceBlock)
    {
        std::copy_n (signal.begin () + static_cast<std::ptrdiff_t> (position), kReferenceBlock, block.begin ());

        bool portFrame = false;
        for (const float sample : block)
            portFrame = analyzer.push (sample) || portFrame;

        const bool referenceFrame = referenceFiddleProcess (reference, block.data (), kReferenceBlock) != 0;
        if (referenceFrame != portFrame)
        {
            check ("frames arrive at the same time", false);
            break;
        }
        if (!referenceFrame)
            continue;

        ++frames;
        worstPower = std::max (worstPower, static_cast<double> (std::abs (referenceFiddleGetTotalPower (reference) - analyzer.getTotalPower ())));

        // Below the floor, the original keeps its old list, which fiddle-floor looks at.
        if (referenceFiddleGetTotalPower (reference) < powerFloor)
            continue;

        int referenceCount = 0;
        for (int p = 0; p < 100; ++p)
        {
            float frequency = 0.0f;
            float amplitude = 0.0f;
            referenceFiddleGetPeak (reference, p, &frequency, &amplitude);
            if (amplitude == 0.0f)
                break;
            ++referenceCount;

            if (p < analyzer.getPeakCount ())
            {
                worstFrequency = std::max (worstFrequency, static_cast<double> (std::abs (frequency - analyzer.getPeaks ()[p].frequency)));
                worstAmplitude = std::max (worstAmplitude, static_cast<double> (std::abs (amplitude - analyzer.getPeaks ()[p].amplitude)));
                ++peaksCompared;
            }
        }
        if (referenceCount != analyzer.getPeakCount ())
            ++countMismatches;
    }

    referenceFiddleDestroy (reference);

    std::printf ("  %s: %d frames, %d peaks, worst %.5f Hz, amplitude %.7f, power %.5f, %d frames with a different count\n",
                 name, frames, peaksCompared, worstFrequency, worstAmplitude, worstPower, countMismatches);
    check ((std::string (name) + " has peaks to compare").c_str (), peaksCompared >= frames);
    check ((std::string (name) + " matches the original").c_str (), countMismatches == 0 && worstFrequency < 0.005 && worstAmplitude < 5e-6 && worstPower < 5e-4);
}

SpectralPeak findStrongest (const FiddleAnalyzer& analyzer)
{
    SpectralPeak strongest { 0.0f, 0.0f };
    for (int p = 0; p < analyzer.getPeakCount (); ++p)
    {
        if (analyzer.getPeaks ()[p].amplitude > strongest.amplitude)
            strongest = analyzer.getPeaks ()[p];
    }
    return strongest;
}

void runThrough (FiddleAnalyzer& analyzer, const Samples& signal)
{
    for (const float sample : signal)
        analyzer.push (sample);
}

using Targets = PartialTracker::Targets;

std::vector<SpectralPeak> peaksOf (const std::vector<std::pair<float, float>>& list)
{
    std::vector<SpectralPeak> peaks;
    for (const auto& entry : list)
        peaks.push_back ({ entry.first, entry.second });
    return peaks;
}

const Targets& feed (PartialTracker& tracker, const std::vector<SpectralPeak>& peaks)
{
    return tracker.update (peaks.data (), static_cast<int> (peaks.size ()));
}

int voiceAt (const Targets& targets, float frequency)
{
    for (int v = 0; v < kVoiceCount; ++v)
    {
        if (targets[static_cast<size_t> (v)].amplitude > 0.0f && std::abs (targets[static_cast<size_t> (v)].frequency - frequency) < 0.5f)
            return v;
    }
    return -1;
}

} // namespace

void testFiddleMatch ()
{
    constexpr size_t kLength = 96000;

    compareWithOriginal ("a single sine", makeTones ({ { 440.0, 0.5 } }, kLength));
    compareWithOriginal ("five inharmonic sines", makeTones ({ { 233.0, 0.4 }, { 587.0, 0.2 }, { 1234.0, 0.1 }, { 3100.0, 0.05 }, { 7001.0, 0.02 } }, kLength));

    std::vector<Tone> harmonics;
    for (int h = 1; h <= 40; ++h)
        harmonics.push_back ({ 110.0 * h, 0.4 / h });
    compareWithOriginal ("a sawtooth-like tone", makeTones (harmonics, kLength));

    compareWithOriginal ("tones in noise", add (makeTones ({ { 330.0, 0.3 }, { 1500.0, 0.2 } }, kLength), makeNoise (0.003, kLength)));

    Samples sweep (kLength);
    double phase = 0.0;
    for (size_t i = 0; i < kLength; ++i)
    {
        phase += 2.0 * 3.14159265358979 * (100.0 * std::pow (2.0, 5.0 * static_cast<double> (i) / kLength)) / kSampleRate;
        sweep[i] = static_cast<float> (0.5 * std::sin (phase));
    }
    compareWithOriginal ("a sweep", sweep);

    compareWithOriginal ("onsets out of silence", concatenate (Samples (5000, 0.0f), makeTones ({ { 700.0, 0.5 }, { 1300.0, 0.25 } }, kLength)));

    compareWithOriginal ("another window size", makeTones ({ { 440.0, 0.5 }, { 1234.0, 0.1 } }, kLength), 4096, 96000.0);
    compareWithOriginal ("a smaller window", makeTones ({ { 700.0, 0.5 } }, kLength), 1024, 44100.0);
}

void testFiddleAccuracy ()
{
    const double kFrequencies[] = { 110.0, 440.3, 1000.0, 5555.5, 15000.0 };
    for (const double frequency : kFrequencies)
    {
        FiddleAnalyzer analyzer;
        analyzer.prepare (kWindow, kSampleRate);
        runThrough (analyzer, makeTones ({ { frequency, 0.5 } }, 9 * kHop));

        const auto strongest = findStrongest (analyzer);
        std::printf ("  %.1f Hz at 0.5 reads as %.3f Hz at %.4f\n", frequency, strongest.frequency, strongest.amplitude);
        check ("the frequency is right to within 0.1%", std::abs (strongest.frequency - frequency) < frequency * 0.001);
        check ("the amplitude is right to within 5%", std::abs (strongest.amplitude - 0.5f) < 0.025f);
    }
}

void testFiddleFloor ()
{
    constexpr float kFloor = 40.0f;
    const auto loud = makeTones ({ { 440.0, 0.5 } }, 6 * kHop);
    const Samples silence (6 * kHop, 0.0f);

    FiddleAnalyzer analyzer;
    analyzer.prepare (kWindow, kSampleRate);
    analyzer.setPowerFloor (kFloor);
    runThrough (analyzer, loud);
    check ("the tone has a peak", analyzer.getPeakCount () > 0);
    runThrough (analyzer, silence);
    check ("in silence, the port has no peaks", analyzer.getPeakCount () == 0);

    // This documents the difference from the original, and why there is one: its list goes on showing the tone.
    void* reference = referenceFiddleCreate (kWindow, static_cast<float> (kSampleRate), kFloor);
    auto signal = concatenate (loud, silence);
    for (size_t position = 0; position + kReferenceBlock <= signal.size (); position += kReferenceBlock)
        referenceFiddleProcess (reference, signal.data () + position, kReferenceBlock);
    float frequency = 0.0f;
    float amplitude = 0.0f;
    referenceFiddleGetPeak (reference, 0, &frequency, &amplitude);
    std::printf ("  in silence, the original still reports %.1f Hz at %.3f\n", frequency, amplitude);
    // The last frame above the floor straddles the end of the tone, so this is about half the tone's level.
    check ("the original keeps reporting the old peak", amplitude > 0.1f);
    referenceFiddleDestroy (reference);
}

void testFiddlePower ()
{
    for (const int window : { 2048, 4096, 8192 })
    {
        FiddleAnalyzer analyzer;
        analyzer.prepare (window, kSampleRate);
        runThrough (analyzer, makeTones ({ { 1000.0, 1.0 } }, static_cast<size_t> (window) * 3));
        const float difference = analyzer.getTotalPower () - FiddleAnalyzer::getFullScalePower (window);
        std::printf ("  window %d: a full-scale sine reads %.2f, against %.2f\n", window, analyzer.getTotalPower (), FiddleAnalyzer::getFullScalePower (window));
        check ("the conversion to dB below full scale is right to within 0.5 dB", std::abs (difference) < 0.5f);
    }
}

void testFiddleSampleRates ()
{
    const struct
    {
        double sampleRate;
        int window;
    } kCases[] = { { 44100.0, 2048 }, { 96000.0, 4096 }, { 192000.0, 8192 } };

    for (const auto& testCase : kCases)
    {
        FiddleAnalyzer analyzer;
        analyzer.prepare (testCase.window, testCase.sampleRate);
        runThrough (analyzer, makeTones ({ { 1000.0, 0.5 } }, static_cast<size_t> (testCase.window) * 4, testCase.sampleRate));
        const auto strongest = findStrongest (analyzer);
        std::printf ("  %.0f Hz: 1000 Hz reads as %.3f Hz\n", testCase.sampleRate, strongest.frequency);
        check ("the frequency is right", std::abs (strongest.frequency - 1000.0f) < 1.0f);
    }
}

void testTrackerLoudest ()
{
    PartialTracker tracker;
    const auto& targets = feed (tracker, peaksOf ({ { 100, 0.01f }, { 200, 0.5f }, { 300, 0.02f }, { 400, 0.4f }, { 500, 0.3f }, { 600, 0.2f }, { 700, 0.1f } }));

    check ("the loudest are played", voiceAt (targets, 200) >= 0 && voiceAt (targets, 400) >= 0 && voiceAt (targets, 500) >= 0 && voiceAt (targets, 600) >= 0 && voiceAt (targets, 700) >= 0);
    check ("the quieter ones, though lower, aren't", voiceAt (targets, 100) < 0 && voiceAt (targets, 300) < 0);
    check ("every voice starting is marked as new", std::all_of (targets.begin (), targets.end (), [] (const PartialTracker::Target& target) { return target.isNew; }));
}

void testTrackerContinuity ()
{
    PartialTracker tracker;
    Targets first = feed (tracker, peaksOf ({ { 440, 0.5f }, { 880, 0.25f } }));
    const int voice440 = voiceAt (first, 440);
    const int voice880 = voiceAt (first, 880);

    // A lower peak arrives. In the original's list, this would move the other two down a place.
    Targets second = feed (tracker, peaksOf ({ { 220, 0.3f }, { 440, 0.5f }, { 880, 0.25f } }));
    check ("the voice at 440 Hz stays on it", voiceAt (second, 440) == voice440);
    check ("the voice at 880 Hz stays on it", voiceAt (second, 880) == voice880);
    check ("those aren't new", !second[static_cast<size_t> (voice440)].isNew && !second[static_cast<size_t> (voice880)].isNew);
    check ("the lower one has its own voice", voiceAt (second, 220) >= 0 && voiceAt (second, 220) != voice440 && voiceAt (second, 220) != voice880);
    check ("that one is new", second[static_cast<size_t> (voiceAt (second, 220))].isNew);

    // A slide, within what still counts as the same partial.
    Targets third = feed (tracker, peaksOf ({ { 220, 0.3f }, { 450, 0.5f }, { 880, 0.25f } }));
    check ("a voice follows its partial as it moves", voiceAt (third, 450) == voice440);

    // Crossing paths doesn't swap voices when each still has a closer peak.
    Targets fourth = feed (tracker, peaksOf ({ { 222, 0.3f }, { 452, 0.5f }, { 884, 0.25f } }));
    check ("each voice takes the peak closest to it", voiceAt (fourth, 452) == voice440 && voiceAt (fourth, 884) == voice880);
}

void testTrackerFade ()
{
    PartialTracker tracker;
    Targets first = feed (tracker, peaksOf ({ { 440, 0.5f } }));
    const auto voice = static_cast<size_t> (voiceAt (first, 440));

    Targets second = feed (tracker, {});
    check ("with no peak, the voice's level goes to zero", second[voice].amplitude == 0.0f);
    check ("and it keeps its pitch as it fades", second[voice].frequency == 440.0f);
}

void testTrackerNewWaits ()
{
    PartialTracker tracker;
    Targets first = feed (tracker, peaksOf ({ { 100, 0.5f }, { 200, 0.5f }, { 300, 0.5f }, { 400, 0.5f }, { 500, 0.5f } }));
    check ("all the voices are playing", std::all_of (first.begin (), first.end (), [] (const PartialTracker::Target& target) { return target.amplitude > 0.0f; }));

    // Five different partials, too far away to be the same ones.
    const auto next = peaksOf ({ { 150, 0.5f }, { 250, 0.5f }, { 350, 0.5f }, { 450, 0.5f }, { 550, 0.5f } });
    Targets second = feed (tracker, next);
    bool allFading = true;
    bool keptPitch = true;
    for (size_t v = 0; v < kVoiceCount; ++v)
    {
        allFading = allFading && second[v].amplitude == 0.0f && !second[v].isNew;
        keptPitch = keptPitch && second[v].frequency == first[v].frequency;
    }
    check ("a voice being heard doesn't change pitch: all of them fade first", allFading && keptPitch);

    Targets third = feed (tracker, next);
    check ("then the new partials start, once the voices are silent",
           voiceAt (third, 150) >= 0 && voiceAt (third, 250) >= 0 && voiceAt (third, 350) >= 0 && voiceAt (third, 450) >= 0 && voiceAt (third, 550) >= 0);
}

void testTrackerKeepsPlace ()
{
    PartialTracker tracker;
    feed (tracker, peaksOf ({ { 100, 0.5f }, { 200, 0.5f }, { 300, 0.5f }, { 400, 0.5f }, { 500, 0.100f } }));

    // A sixth peak, slightly louder than the fifth one and then slightly quieter, in turn.
    int changes = 0;
    for (int frame = 0; frame < 20; ++frame)
    {
        const float a = frame % 2 == 0 ? 0.110f : 0.095f;
        const Targets targets = feed (tracker, peaksOf ({ { 100, 0.5f }, { 200, 0.5f }, { 300, 0.5f }, { 400, 0.5f }, { 500, 0.100f }, { 600, a } }));
        if (voiceAt (targets, 500) < 0)
            ++changes;
    }
    check ("a peak being played isn't displaced by one that's about the same level", changes == 0);

    Targets targets = feed (tracker, peaksOf ({ { 100, 0.5f }, { 200, 0.5f }, { 300, 0.5f }, { 400, 0.5f }, { 500, 0.1f }, { 600, 0.3f } }));
    check ("a clearly louder one does", voiceAt (targets, 500) < 0);
    targets = feed (tracker, peaksOf ({ { 100, 0.5f }, { 200, 0.5f }, { 300, 0.5f }, { 400, 0.5f }, { 500, 0.1f }, { 600, 0.3f } }));
    check ("and gets the voice in the next frame", voiceAt (targets, 600) >= 0);
}

} // namespace Tests
} // namespace PartialResynth
