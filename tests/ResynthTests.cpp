// The whole path through the built plug-in: what goes in is analysed, and comes out played by the voices.

#include "Harness.h"
#include "Tests.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace PartialResynth {
namespace Tests {

namespace {

constexpr size_t kSettle = 9600; // 200 ms, for a measurement to start after the plug-in has settled.

size_t seconds (double value)
{
    return static_cast<size_t> (value * kSampleRate);
}

} // namespace

void testResynthPartials ()
{
    Plug plug;
    const std::vector<Tone> tones { { 233.0, 0.3 }, { 587.0, 0.2 }, { 1234.0, 0.1 }, { 3100.0, 0.05 }, { 7001.0, 0.025 } };
    const auto input = makeTones (tones, seconds (1.0));
    const auto output = plug.process (input);

    Samples residual = slice (output.left, kSettle, output.left.size ());
    for (const auto& tone : tones)
    {
        const double level = toneLevel (output.left, tone.frequency, kSettle);
        std::printf ("  %.0f Hz: in %.1f dB, out %.1f dB\n", tone.frequency, db (tone.amplitude), db (level));
        check ("the partial comes out at its level, within 0.5 dB", std::abs (db (level) - db (tone.amplitude)) < 0.5);
        check ("and on its frequency", std::abs (measureFrequency (slice (output.left, kSettle, seconds (0.5)), tone.frequency) - tone.frequency) < tone.frequency * 0.002);
    }
    check ("left and right are the same with Spread at 0", output.left == output.right);
}

void testResynthLoudest ()
{
    Plug plug;
    const std::vector<Tone> tones { { 150.0, 0.01 }, { 300.0, 0.012 }, { 500.0, 0.3 }, { 1100.0, 0.25 }, { 2300.0, 0.2 }, { 4700.0, 0.15 }, { 9100.0, 0.1 } };
    const auto output = plug.process (makeTones (tones, seconds (1.0)));

    for (const auto& tone : tones)
        std::printf ("  %.0f Hz at %.1f dB comes out at %.1f dB\n", tone.frequency, db (tone.amplitude), db (toneLevel (output.left, tone.frequency, kSettle)));

    check ("the quiet low partials don't get voices", toneLevel (output.left, 150.0, kSettle) < 0.001 && toneLevel (output.left, 300.0, kSettle) < 0.001);
    check ("the five loud ones do", toneLevel (output.left, 500.0, kSettle) > 0.25 && toneLevel (output.left, 1100.0, kSettle) > 0.2
                                        && toneLevel (output.left, 2300.0, kSettle) > 0.16 && toneLevel (output.left, 4700.0, kSettle) > 0.12
                                        && toneLevel (output.left, 9100.0, kSettle) > 0.08);
}

// A partial arriving below the others is the case the original's list order gets wrong, by renumbering them.
void testResynthContinuous ()
{
    Plug plug;
    const size_t kTotal = seconds (1.0);
    auto input = makeTones ({ { 1000.0, 0.2 }, { 2000.0, 0.1 } }, kTotal);
    const auto lower = makeTones ({ { 300.0, 0.3 } }, kTotal - seconds (0.5));
    for (size_t i = 0; i < lower.size (); ++i)
        input[seconds (0.5) + i] += lower[i];

    const auto output = plug.process (input);

    double lowest = 1.0;
    double highest = 0.0;
    constexpr size_t kWindow = 1024;
    for (size_t from = kSettle; from + kWindow < kTotal; from += kWindow / 4)
    {
        const double level = toneLevel (slice (output.left, from, kWindow), 1000.0);
        lowest = std::min (lowest, level);
        highest = std::max (highest, level);
    }
    std::printf ("  1000 Hz across the arrival of 300 Hz: %.1f to %.1f dB\n", db (lowest), db (highest));
    check ("the existing partial's level holds steady within 2 dB", db (highest) - db (lowest) < 2.0);
    check ("and the new one comes in", toneLevel (output.left, 300.0, seconds (0.7)) > 0.25);
}

// The pitch moves through the frames, without the voice dropping out or stepping.
void testResynthFollows ()
{
    Plug plug;
    const size_t kTotal = seconds (2.0);
    Samples input (kTotal);
    double phase = 0.0;
    for (size_t i = 0; i < kTotal; ++i)
    {
        // Vibrato of a semitone each way at 5 Hz.
        const double note = std::sin (2.0 * 3.14159265358979 * 5.0 * static_cast<double> (i) / kSampleRate);
        phase += 2.0 * 3.14159265358979 * 440.0 * std::pow (2.0, note / 12.0) / kSampleRate;
        input[i] = static_cast<float> (0.5 * std::sin (phase));
    }
    const auto output = plug.process (input);

    double lowest = 1.0;
    constexpr size_t kWindow = 1024;
    for (size_t from = kSettle; from + kWindow < kTotal; from += kWindow / 2)
        lowest = std::min (lowest, rms (slice (output.left, from, kWindow)));
    std::printf ("  lowest level in the vibrato: %.1f dB RMS\n", db (lowest));
    check ("the voice keeps sounding all the way through the vibrato", db (lowest) > -12.0);
}

void testResynthSilence ()
{
    Plug plug;
    const auto input = concatenate (makeTones ({ { 440.0, 0.5 } }, seconds (0.5)), Samples (seconds (1.0), 0.0f));
    const auto output = plug.process (input);

    check ("the tone is heard", rms (slice (output.left, seconds (0.1), seconds (0.3))) > 0.2);
    const double tail = peak (output.left, seconds (0.5) + 2 * 2048);
    std::printf ("  peak after the tail: %.1f dB\n", db (tail));
    check ("after the input stops, the voices stop rather than drone", tail == 0.0);

    bool isFlagged = false;
    for (size_t block = output.blockSilent.size () - 10; block < output.blockSilent.size (); ++block)
        isFlagged = isFlagged || output.blockSilent[block];
    check ("and the silence flags say so", isFlagged && output.blockSilent.back ());

    size_t firstFlagged = output.blockSilent.size ();
    for (size_t block = 0; block < output.blockSilent.size (); ++block)
    {
        if (output.blockSilent[block])
        {
            firstFlagged = block;
            break;
        }
    }
    bool isFlagTruthful = true;
    for (size_t block = firstFlagged; block < output.blockSilent.size (); ++block)
        isFlagTruthful = isFlagTruthful && output.blockSilent[block];
    check ("no sound comes out in a block flagged as silent", isFlagTruthful && peak (output.left, firstFlagged * kBlockSize) == 0.0);

    // Sound comes back, after sleeping.
    const auto again = plug.process (makeTones ({ { 660.0, 0.5 } }, seconds (0.5)));
    check ("it wakes up for the next sound", toneLevel (again.left, 660.0, kSettle) > 0.4);
}

void testResynthThreshold ()
{
    const auto quiet = makeTones ({ { 440.0, 0.003 } }, seconds (0.5)); // -50 dB

    {
        Plug plug;
        plug.set (kThreshold, -60.0);
        const double level = toneLevel (plug.process (quiet).left, 440.0, kSettle);
        std::printf ("  threshold -60 dB: %.1f dB\n", db (level));
        check ("a -50 dB tone passes a -60 dB threshold", std::abs (db (level) - db (0.003)) < 1.0);
    }
    {
        Plug plug;
        plug.set (kThreshold, -40.0);
        const auto output = plug.process (quiet);
        std::printf ("  threshold -40 dB: peak %.1f dB\n", db (peak (output.left)));
        check ("but not a -40 dB one, and is silent rather than held", peak (output.left) == 0.0);
    }
    {
        // Falling below it, from above: the plug-in must not stay on the last peaks, as the original's list does.
        Plug plug;
        plug.set (kThreshold, -40.0);
        const auto input = concatenate (makeTones ({ { 440.0, 0.3 } }, seconds (0.5)), quiet);
        const auto output = plug.process (input);
        check ("a tone falling below it stops being played", peak (output.left, seconds (0.6)) == 0.0);
    }
}

void testLatencyAlignment ()
{
    Plug plug;
    const size_t kOnset = seconds (0.25);
    const auto input = concatenate (Samples (kOnset, 0.0f), makeTones ({ { 1000.0, 0.5 } }, seconds (0.5)));

    // The dry path is exact.
    plug.set (kDryWet, 0.0);
    const auto dry = plug.process (input);
    size_t firstDry = 0;
    while (firstDry < dry.left.size () && dry.left[firstDry] == 0.0f)
        ++firstDry;
    std::printf ("  dry starts %zu samples after the input\n", firstDry > kOnset ? firstDry - kOnset - 1 : 0);
    check ("the dry signal is delayed by exactly the latency", firstDry - kOnset - 1 == kLatency);

    // The envelope reaches half way, in amplitude, when the window is half full of the tone.
    Plug wet;
    const auto output = wet.process (input);
    size_t halfWay = 0;
    constexpr size_t kWindow = 64;
    for (size_t from = kOnset; from + kWindow < output.left.size (); from += 8)
    {
        if (peak (slice (output.left, from, kWindow)) >= 0.25)
        {
            halfWay = from + kWindow / 2 - kOnset;
            break;
        }
    }
    const double difference = static_cast<double> (halfWay) - static_cast<double> (kLatency);
    std::printf ("  the voice reaches half level %zu samples after the onset, %.0f from the reported latency\n", halfWay, difference);
    check ("the voice is within 5 ms of the dry signal", std::abs (difference) < 240.0);
}

} // namespace Tests
} // namespace PartialResynth
