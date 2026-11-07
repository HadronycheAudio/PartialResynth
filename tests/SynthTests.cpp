// The voices' controls, heard through the built plug-in. A sine going in plays one voice, and what that voice
// does to it shows what the controls do.

#include "Harness.h"
#include "Tests.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace Steinberg;

namespace PartialResynth {
namespace Tests {

namespace {

constexpr size_t kSettle = 9600;

// A level to compare with: the -6 dB tone every test here plays through one voice.
constexpr double kAmplitude = 0.5;

Samples steadyTone (double frequency, double seconds = 1.0)
{
    return makeTones ({ { frequency, kAmplitude } }, static_cast<size_t> (seconds * kSampleRate));
}

} // namespace

void testOscWaves ()
{
    constexpr double kFrequency = 440.0;

    {
        Plug plug;
        const auto output = plug.process (steadyTone (kFrequency));
        check ("sine: no second harmonic", db (toneLevel (output.left, 2 * kFrequency, kSettle)) < -70.0);
        check ("sine: at the level of the partial", std::abs (toneLevel (output.left, kFrequency, kSettle) - kAmplitude) < 0.03);
    }
    {
        Plug plug;
        plug.set (kOsc1Wave, 1);
        const auto output = plug.process (steadyTone (kFrequency));
        const double first = toneLevel (output.left, kFrequency, kSettle);
        const double second = toneLevel (output.left, 2 * kFrequency, kSettle);
        const double third = toneLevel (output.left, 3 * kFrequency, kSettle);
        std::printf ("  saw: 2nd at %.1f dB, 3rd at %.1f dB relative to the 1st\n", db (second / first), db (third / first));
        check ("saw: harmonics fall as 1/n", std::abs (db (second / first) + 6.02) < 0.5 && std::abs (db (third / first) + 9.54) < 0.5);
    }
    {
        Plug plug;
        plug.set (kOsc1Wave, 2);
        const auto output = plug.process (steadyTone (kFrequency));
        const double first = toneLevel (output.left, kFrequency, kSettle);
        const double second = toneLevel (output.left, 2 * kFrequency, kSettle);
        const double third = toneLevel (output.left, 3 * kFrequency, kSettle);
        std::printf ("  square: 2nd at %.1f dB, 3rd at %.1f dB relative to the 1st\n", db (second / first), db (third / first));
        check ("square: only odd harmonics", db (second / first) < -60.0);
        check ("square: they fall as 1/n", std::abs (db (third / first) + 9.54) < 0.5);
    }
}

void testOscTuning ()
{
    {
        Plug plug;
        plug.set (kOsc1Coarse, 12);
        const auto output = plug.process (steadyTone (440.0));
        check ("coarse +12 plays an octave up", toneLevel (output.left, 880.0, kSettle) > 0.45 && toneLevel (output.left, 440.0, kSettle) < 0.001);
    }
    {
        Plug plug;
        plug.set (kOsc1Coarse, -12);
        const auto output = plug.process (steadyTone (440.0));
        check ("coarse -12 plays an octave down", toneLevel (output.left, 220.0, kSettle) > 0.45);
    }
    {
        Plug plug;
        plug.set (kOsc1Fine, 100);
        const auto output = plug.process (steadyTone (440.0));
        const double measured = measureFrequency (slice (output.left, kSettle, 24000), 466.16);
        std::printf ("  fine +100: %.2f Hz\n", measured);
        check ("fine +100 is a semitone up", std::abs (measured - 466.16) < 0.5);
    }
    {
        Plug plug;
        plug.set (kOsc2Level, 100);
        plug.set (kOsc2Wave, 0);
        plug.set (kOsc2Coarse, 7);
        const auto output = plug.process (steadyTone (440.0));
        check ("oscillator 2 tunes separately", toneLevel (output.left, 440.0 * std::pow (2.0, 7.0 / 12.0), kSettle) > 0.45 && toneLevel (output.left, 440.0, kSettle) > 0.45);
    }
}

void testOscLevels ()
{
    {
        Plug plug;
        plug.set (kOsc1Level, 0);
        check ("with both levels at 0, nothing is heard", peak (plug.process (steadyTone (440.0)).left, kSettle) == 0.0);
    }
    {
        Plug plug;
        plug.set (kOsc1Level, 50);
        const double level = toneLevel (plug.process (steadyTone (440.0)).left, 440.0, kSettle);
        check ("level 50% is half the amplitude", std::abs (level - 0.5 * kAmplitude) < 0.02);
    }
    {
        Plug plug;
        plug.set (kOsc1Level, 100);
        plug.set (kOsc2Level, 100);
        plug.set (kOsc2Wave, 0);
        const double level = toneLevel (plug.process (steadyTone (440.0)).left, 440.0, kSettle);
        std::printf ("  two sines in unison: %.3f\n", level);
        check ("two in unison add up", std::abs (level - 2.0 * kAmplitude) < 0.05);
    }
    {
        // Both oscillators' controls apply to all the voices: one knob, every partial.
        Plug plug;
        plug.set (kOsc1Level, 50);
        const std::vector<Tone> tones { { 300.0, 0.2 }, { 800.0, 0.2 }, { 1500.0, 0.2 }, { 2500.0, 0.2 }, { 4000.0, 0.2 } };
        const auto output = plug.process (makeTones (tones, 48000));
        bool allHalved = true;
        for (const auto& tone : tones)
            allHalved = allHalved && std::abs (toneLevel (output.left, tone.frequency, kSettle) - 0.1) < 0.01;
        check ("one control applies to all five voices", allHalved);
    }
}

void testOscAliasing ()
{
    constexpr double kFrequency = 3000.0;
    for (const int wave : { 1, 2 })
    {
        Plug plug;
        plug.set (kOsc1Wave, wave);
        const auto output = plug.process (steadyTone (kFrequency));

        // Harmonics of 3 kHz at 48 kHz fold back on to harmonics, so this measures between them.
        double worst = 0.0;
        for (double hz = 500.0; hz < 22000.0; hz += 1000.0)
            worst = std::max (worst, toneLevel (output.left, hz + 250.0, kSettle));
        const double first = toneLevel (output.left, kFrequency, kSettle);
        std::printf ("  wave %d at 3 kHz: strongest line off the harmonics at %.1f dB\n", wave, db (worst / first));
        check ("nothing strong lands between the harmonics", db (worst / first) < -50.0);
    }

    // A partial with a tuning that takes it past where a saw can be played doesn't fold over.
    Plug plug;
    plug.set (kOsc1Wave, 1);
    plug.set (kOsc1Coarse, 24);
    const auto output = plug.process (steadyTone (10000.0));
    check ("a saw above the top of its range is silent", rms (output.left, kSettle) < 1e-4);
}

void testFilterModes ()
{
    const auto levelWith = [] (int mode, double cutoff, double tone) {
        Plug plug;
        plug.set (kFilterMode, mode);
        plug.set (kFilterCutoff, cutoff);
        return toneLevel (plug.process (steadyTone (tone)).left, tone, kSettle);
    };

    const double lowPass = db (levelWith (0, 200.0, 2000.0) / kAmplitude);
    std::printf ("  low-pass at 200 Hz: 2 kHz at %.1f dB\n", lowPass);
    check ("low-pass cuts above the cutoff", lowPass < -35.0);
    check ("and passes below it", db (levelWith (0, 2000.0, 200.0) / kAmplitude) > -1.0);

    const double highPass = db (levelWith (1, 2000.0, 200.0) / kAmplitude);
    std::printf ("  high-pass at 2 kHz: 200 Hz at %.1f dB\n", highPass);
    check ("high-pass cuts below the cutoff", highPass < -35.0);
    check ("and passes above it", db (levelWith (1, 200.0, 2000.0) / kAmplitude) > -1.0);

    check ("band-pass passes the cutoff", db (levelWith (2, 1000.0, 1000.0) / kAmplitude) > -1.0);
    check ("and cuts away from it", db (levelWith (2, 1000.0, 100.0) / kAmplitude) < -15.0);

    check ("notch cuts the cutoff", db (levelWith (3, 1000.0, 1000.0) / kAmplitude) < -30.0);
    check ("and passes away from it", db (levelWith (3, 1000.0, 100.0) / kAmplitude) > -1.0);
}

void testFilterResonance ()
{
    const auto levelWith = [] (double resonance) {
        Plug plug;
        plug.set (kFilterCutoff, 1000.0);
        plug.set (kFilterResonance, resonance);
        return toneLevel (plug.process (steadyTone (1000.0)).left, 1000.0, kSettle);
    };

    const double flat = db (levelWith (0.0) / kAmplitude);
    const double peaked = db (levelWith (100.0) / kAmplitude);
    std::printf ("  a tone on the cutoff: %.1f dB at no resonance, %.1f dB at full\n", flat, peaked);
    check ("no resonance has no peak", flat < 0.0);
    check ("full resonance has a strong one", peaked > 20.0);
}

void testKeyFollow ()
{
    const auto levelWith = [] (double follow, double tone) {
        Plug plug;
        plug.set (kFilterCutoff, 440.0);
        plug.set (kFilterKeyFollow, follow);
        return db (toneLevel (plug.process (steadyTone (tone)).left, tone, kSettle) / kAmplitude);
    };

    const double fixed = levelWith (0.0, 3520.0);
    const double following = levelWith (100.0, 3520.0);
    const double half = levelWith (50.0, 3520.0);
    std::printf ("  three octaves above the cutoff: %.1f dB fixed, %.1f dB at half follow, %.1f dB at full follow\n", fixed, half, following);
    check ("a fixed cutoff cuts a higher partial", fixed < -30.0);
    check ("full follow keeps the cutoff with the partial, at Butterworth's -3 dB", std::abs (following + 3.0) < 1.0);
    check ("half follow is in between", half > fixed + 10.0 && half < following - 5.0);
    check ("at 440 Hz, follow makes no difference", std::abs (levelWith (0.0, 440.0) - levelWith (100.0, 440.0)) < 0.5);
}

void testEnvToCutoff ()
{
    const auto levelWith = [] (double amount, double amplitude) {
        Plug plug;
        plug.set (kFilterCutoff, 100.0);
        plug.set (kFilterEnvAmount, amount);
        const auto input = makeTones ({ { 3200.0, amplitude } }, 48000);
        return db (toneLevel (plug.process (input).left, 3200.0, kSettle) / amplitude);
    };

    const double none = levelWith (0.0, 0.5);
    const double opened = levelWith (8.0, 0.5);
    const double quieter = levelWith (8.0, 0.05);
    const double closed = levelWith (-8.0, 0.5);
    std::printf ("  3.2 kHz with cutoff at 100 Hz: %.1f dB with no amount, %.1f dB at +8, %.1f dB at +8 for a tone 20 dB quieter, %.1f dB at -8\n", none, opened, quieter, closed);
    check ("a positive amount opens the filter", opened > none + 30.0);
    // At +8 octaves, the 0.05 tone's cutoff is 2.2 kHz, half an octave below it, which works out at about -7 dB.
    check ("a quieter partial opens it less", quieter < opened - 5.0);
    check ("a negative amount closes it", closed < none - 15.0);
}

void testGlide ()
{
    // A step of 40 cents, which still counts as the same partial, so the voice stays and moves.
    const double kFrom = 1000.0;
    const double kTo = 1000.0 * std::pow (2.0, 0.4 / 12.0);
    const size_t kStep = 48000;
    const auto input = concatenate (makeTones ({ { kFrom, kAmplitude } }, kStep), makeTones ({ { kTo, kAmplitude } }, 24000));

    const auto frequencyAfterStep = [&] (double glideMs) {
        Plug plug;
        plug.set (kGlide, glideMs);
        const auto output = plug.process (input);
        return measureFrequency (slice (output.left, kStep + 4800, 4800), kFrom);
    };

    const double tight = frequencyAfterStep (0.0);
    const double glided = frequencyAfterStep (2000.0);
    std::printf ("  100 to 200 ms after a step from %.1f to %.1f Hz: %.2f Hz with no glide, %.2f Hz at 2 s\n", kFrom, kTo, tight, glided);
    check ("with no glide, it's already there", std::abs (tight - kTo) < 1.5);
    check ("with a long glide, it's still on its way", glided < kFrom + 0.5 * (kTo - kFrom));

    Plug plug;
    plug.set (kGlide, 200.0);
    const auto output = plug.process (concatenate (input, makeTones ({ { kTo, kAmplitude } }, 48000)));
    const double measured = measureFrequency (slice (output.left, output.left.size () - 24000, 24000), kFrom);
    std::printf ("  long after that step, with a 200 ms glide: %.2f Hz\n", measured);
    check ("and with a glide, it gets there in the end", std::abs (measured - kTo) < 1.0);

    // A partial that starts out of silence has nothing to glide from.
    Plug start;
    start.set (kGlide, 2000.0);
    const auto started = start.process (steadyTone (1000.0, 0.5));
    const double startedAt = measureFrequency (slice (started.left, kSettle, 9600), 1000.0);
    std::printf ("  200 to 400 ms after a 1000 Hz tone starts, with a 2 s glide: %.2f Hz\n", startedAt);
    check ("a new partial starts on its pitch, whatever the glide", std::abs (startedAt - 1000.0) < 2.0);
}

void testSpread ()
{
    const std::vector<Tone> tones { { 300.0, 0.2 }, { 800.0, 0.2 }, { 1500.0, 0.2 }, { 2500.0, 0.2 }, { 4000.0, 0.2 } };
    const auto input = makeTones (tones, 48000);

    {
        Plug plug;
        plug.set (kSpread, 0);
        const auto output = plug.process (input);
        check ("at 0, left and right are the same", output.left == output.right);
    }

    Plug plug;
    plug.set (kSpread, 100);
    const auto output = plug.process (input);
    check ("at 100, they aren't", output.left != output.right);

    int leftOnly = 0;
    int rightOnly = 0;
    int centred = 0;
    int sides = 0;
    bool powerHolds = true;
    for (const auto& tone : tones)
    {
        const double left = toneLevel (output.left, tone.frequency, kSettle);
        const double right = toneLevel (output.right, tone.frequency, kSettle);
        std::printf ("  %.0f Hz: left %.1f dB, right %.1f dB\n", tone.frequency, db (left), db (right));

        if (left > 0.1 && right < 0.003)
            ++leftOnly;
        else if (right > 0.1 && left < 0.003)
            ++rightOnly;
        else if (std::abs (db (left) - db (right)) < 0.5)
            ++centred;
        else if (left > 0.0 && right > 0.0)
            ++sides;

        // Constant power, with 2 being the total for a voice in the middle.
        powerHolds = powerHolds && std::abs (db (std::sqrt ((left * left + right * right) / 2.0)) - db (0.2)) < 0.5;
    }
    check ("the voices cover the field: one in the middle, two part way, two at the sides", centred == 1 && sides == 2 && leftOnly == 1 && rightOnly == 1);
    check ("at constant power", powerHolds);
}

void testDryWet ()
{
    const auto input = makeTones ({ { 1000.0, 0.3 } }, 48000);
    const auto noise = makeNoise (0.2, 48000);

    {
        Plug plug;
        plug.set (kDryWet, 0);
        const auto output = plug.process (noise);
        bool exact = true;
        for (size_t i = 0; i + kLatency < noise.size (); ++i)
            exact = exact && output.left[i + kLatency] == noise[i];
        check ("at 0%, the output is the input, delayed by the latency", exact);
    }
    {
        Plug plug;
        plug.set (kDryWet, 100);
        // Noise has no stable peaks to speak of, so the wet path gives something other than the input.
        const auto output = plug.process (input);
        check ("at 100%, the tone is played by a voice", toneLevel (output.left, 1000.0, kSettle) > 0.25);
    }
    {
        Plug plug;
        plug.set (kDryWet, 50);
        plug.set (kOsc1Coarse, 12);
        const auto output = plug.process (input);
        std::printf ("  at 50%%: dry at %.1f dB, wet at %.1f dB\n", db (toneLevel (output.left, 1000.0, kSettle)), db (toneLevel (output.left, 2000.0, kSettle)));
        check ("at 50%, both are at full level", std::abs (toneLevel (output.left, 1000.0, kSettle) - 0.3) < 0.015 && std::abs (toneLevel (output.left, 2000.0, kSettle) - 0.3) < 0.03);
    }
    {
        Plug plug;
        Samples left = input;
        const Samples right (input.size (), 0.0f);
        plug.set (kDryWet, 0);
        const auto output = plug.process (left, right);
        check ("the dry path stays in stereo", toneLevel (output.left, 1000.0, kSettle) > 0.25 && peak (output.right) == 0.0);
    }
    {
        // Analysis is of both channels, summed: a tone in only one of them plays at half level.
        Plug plug;
        const Samples silence (input.size (), 0.0f);
        const auto output = plug.process (input, silence);
        const double level = toneLevel (output.right, 1000.0, kSettle);
        std::printf ("  a 0.3 tone in the left channel alone plays at %.3f in both\n", level);
        check ("analysis takes both channels, summed", std::abs (level - 0.15) < 0.015);
    }
}

void testVolume ()
{
    const auto input = steadyTone (1000.0);
    const auto levelWith = [&] (double volume) {
        Plug plug;
        plug.set (kVolume, volume);
        return db (toneLevel (plug.process (input).left, 1000.0, kSettle) / kAmplitude);
    };

    check ("0 dB leaves it alone", std::abs (levelWith (0.0)) < 0.5);
    check ("-12 dB takes off 12 dB", std::abs (levelWith (-12.0) + 12.0) < 0.5);
    check ("+6 dB adds 6 dB", std::abs (levelWith (6.0) - 6.0) < 0.5);

    Plug plug;
    plug.set (kVolume, -60.0);
    check ("the bottom of the range is silent", peak (plug.process (input).left) == 0.0);
}

void testBlockSizes ()
{
    const auto input = add (makeTones ({ { 300.0, 0.3 }, { 1100.0, 0.2 }, { 2900.0, 0.1 } }, 48000), makeNoise (0.001, 48000));
    const auto withSilence = concatenate (concatenate (input, Samples (24000, 0.0f)), input);

    Plug reference (kSampleRate, 4096);
    reference.set (kOsc1Wave, 1);
    const auto expected = reference.process (withSilence, 256);

    for (const int blockSize : { 1, 64, 441, 4096 })
    {
        Plug plug (kSampleRate, 4096);
        plug.set (kOsc1Wave, 1);
        const auto output = plug.process (withSilence, blockSize);
        std::printf ("  block size %d\n", blockSize);
        check ("the output doesn't depend on the block size, even across sleeping", output.left == expected.left);
    }
}

void testDeterminism ()
{
    const auto input = add (makeTones ({ { 300.0, 0.3 }, { 1100.0, 0.2 } }, 48000), makeNoise (0.01, 48000));

    Plug first;
    Plug second;
    first.set (kOsc1Wave, 1);
    second.set (kOsc1Wave, 1);
    check ("two instances render the same", first.process (input).left == second.process (input).left);

    const auto again = first.process (Samples (4800, 0.0f));
    first.component->setActive (false);
    first.component->setActive (true);
    Plug third;
    third.set (kOsc1Wave, 1);
    check ("and so does one that has been reactivated", first.process (input).left == third.process (input).left);
}

void testExtremeSettings ()
{
    const auto noise = makeNoise (1.0, 48000);
    const auto input = concatenate (concatenate (noise, Samples (48000, 0.0f)), makeTones ({ { 20.0, 1.0 }, { 23999.0, 1.0 }, { 11000.0, 1.0 } }, 48000));

    for (const double value : { 0.0, 1.0 })
    {
        Plug plug;
        for (Vst::ParamID id = 0; id < kParamCount; ++id)
            plug.set (static_cast<ParamId> (id), plug.controller->normalizedParamToPlain (id, value));
        plug.set (kVolume, 6.0);
        plug.set (kOsc1Level, 100);
        plug.set (kOsc2Level, 100);
        const auto output = plug.process (input);
        check (value == 0.0 ? "everything at minimum stays finite" : "everything at maximum stays finite", isFinite (output.left) && isFinite (output.right));
    }

    // Not a real input, but something might produce it.
    Plug plug;
    Samples broken (48000, 0.0f);
    broken[100] = std::numeric_limits<float>::infinity ();
    broken[200] = std::numeric_limits<float>::quiet_NaN ();
    const auto brokenOutput = plug.process (broken);
    check ("a broken input doesn't reach the output", isFinite (brokenOutput.left));
    const auto after = plug.process (steadyTone (440.0));
    check ("and leaves nothing behind", isFinite (after.left) && toneLevel (after.left, 440.0, kSettle) > 0.4);

    // Running without an input at all.
    Plug noInput;
    float left[256] {};
    float right[256] {};
    float* outputs[2] = { left, right };
    Vst::AudioBusBuffers outputBus {};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = outputs;
    Vst::ProcessData data {};
    data.processMode = Vst::kRealtime;
    data.symbolicSampleSize = Vst::kSample32;
    data.numSamples = 256;
    data.numInputs = 0;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    check ("no input buses is handled", noInput.processor->process (data) == kResultOk);
}

void testParameterState ()
{
    Plug first;
    first.set (kOsc1Wave, 2);
    first.set (kOsc2Level, 75);
    first.set (kFilterCutoff, 500);
    first.set (kSpread, 40);
    first.process (Samples (256, 0.0f));
    const auto state = first.getStateBytes ();

    Plug second;
    check ("the processor and the controller take a saved state", second.setStateBytes (state));
    check ("and it saves back the same", second.getStateBytes () == state);
    check ("and the controller shows its values", std::abs (second.controller->getParamNormalized (kOsc2Level) - 0.75) < 1e-9);
}

void testParameterText ()
{
    Plug plug;

    const auto text = [&] (ParamId id, double plain) {
        return paramText (plug.controller, id, plug.controller->plainParamToNormalized (id, plain));
    };

    check ("wave 0 is Sine", text (kOsc1Wave, 0) == "Sine");
    check ("wave 1 is Saw", text (kOsc1Wave, 1) == "Saw");
    check ("wave 2 is Square", text (kOsc2Wave, 2) == "Square");
    check ("filter mode 1 is High-pass", text (kFilterMode, 1) == "High-pass");
    check ("the bottom of Volume is -inf", text (kVolume, -60.0) == "-inf");
    check ("coarse shows whole semitones", text (kOsc1Coarse, 7) == "7");

    double value = 0.0;
    check ("a typed value is understood", paramFromText (plug.controller, kFilterCutoff, "1000", value));
    check ("and it means what it says", std::abs (plug.controller->normalizedParamToPlain (kFilterCutoff, value) - 1000.0) < 0.01);
}

// Five sawtooth voices with both oscillators and the filter moving, on the kind of input that keeps all five.
void measureCpu ()
{
    std::vector<Tone> tones;
    for (int h = 1; h <= 12; ++h)
        tones.push_back ({ 110.0 * h, 0.4 / h });
    const auto input = makeTones (tones, 48000 * 10);

    Plug plug;
    plug.set (kOsc1Wave, 1);
    plug.set (kOsc2Wave, 2);
    plug.set (kOsc2Level, 100);
    plug.set (kFilterCutoff, 2000);
    plug.set (kFilterEnvAmount, 2);
    plug.set (kSpread, 100);

    const auto start = std::chrono::steady_clock::now ();
    plug.process (input);
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now () - start;

    const double seconds = elapsed.count ();
    std::printf ("  10 s of audio in %.3f s, which is %.2f%% of one core\n", seconds, seconds * 10.0);
}

} // namespace Tests
} // namespace PartialResynth
