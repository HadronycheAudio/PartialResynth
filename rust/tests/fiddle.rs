//! The analysis, compared with Pure Data's original fiddle~, and the tracker that chooses what the voices play. These
//! were the C++ test program's `fiddle-*` and `tracker-*` tests, which compiled the port in directly, because a host
//! can't see what they produce.

// The test signals are the C++ test host's, down to its value of pi.
#![allow(clippy::approx_constant)]

use partialresynth::fiddle_analyzer::{FiddleAnalyzer, SpectralPeak};
use partialresynth::partial_tracker::{PartialTracker, Targets, VOICE_COUNT};
use reference_fiddle::ReferenceFiddle;

const SAMPLE_RATE: f64 = 48000.0;
const WINDOW: usize = 2048;
const HOP: usize = WINDOW / 2;
const REFERENCE_BLOCK: usize = 64;

struct Tone {
    frequency: f64,
    amplitude: f64,
}

fn tone(frequency: f64, amplitude: f64) -> Tone {
    Tone { frequency, amplitude }
}

fn make_tones(tones: &[Tone], count: usize, sample_rate: f64) -> Vec<f32> {
    let mut samples = vec![0f32; count];
    for tone in tones {
        for (i, sample) in samples.iter_mut().enumerate() {
            *sample +=
                (tone.amplitude * (2.0 * 3.14159265358979 * tone.frequency * i as f64 / sample_rate).sin()) as f32;
        }
    }
    samples
}

fn make_noise(amplitude: f64, count: usize) -> Vec<f32> {
    let mut random: u32 = 0x9E37_79B9;
    (0..count)
        .map(|_| {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            (amplitude * (f64::from(random) / 2147483648.0 - 1.0)) as f32
        })
        .collect()
}

fn add(first: &[f32], second: &[f32]) -> Vec<f32> {
    first.iter().zip(second).map(|(a, b)| a + b).collect()
}

fn concatenate(first: &[f32], second: &[f32]) -> Vec<f32> {
    [first, second].concat()
}

fn analyzer(window: usize, sample_rate: f64) -> FiddleAnalyzer {
    let mut analyzer = FiddleAnalyzer::default();
    analyzer.prepare(window, sample_rate, 0);
    analyzer
}

fn run_through(analyzer: &mut FiddleAnalyzer, signal: &[f32]) {
    for &sample in signal {
        analyzer.push(sample);
    }
}

fn strongest(analyzer: &FiddleAnalyzer) -> SpectralPeak {
    analyzer
        .peaks()
        .iter()
        .copied()
        .fold(SpectralPeak::default(), |best, peak| {
            if peak.amplitude > best.amplitude { peak } else { best }
        })
}

/// Runs a signal through the original and the port, and holds the worst difference across every frame to the same
/// limits as the C++ test did: close to what the port achieves on purpose, because looser ones missed a 1% error in
/// a filter coefficient on most of the signals.
fn compare_with_original(name: &str, signal: &[f32], window: usize, sample_rate: f64, power_floor: f32) {
    let mut reference = ReferenceFiddle::new(window, sample_rate, power_floor);
    let mut port = analyzer(window, sample_rate);
    port.set_power_floor(power_floor);

    let mut frames = 0;
    let mut count_mismatches = 0;
    let mut peaks_compared = 0;
    let mut worst_frequency = 0f64;
    let mut worst_amplitude = 0f64;
    let mut worst_power = 0f64;

    for block in signal.chunks_exact(REFERENCE_BLOCK) {
        let mut port_frame = false;
        for &sample in block {
            port_frame = port.push(sample) || port_frame;
        }

        let reference_frame = reference.process(block);
        assert_eq!(reference_frame, port_frame, "{name}: frames arrive at the same time");
        if !reference_frame {
            continue;
        }

        frames += 1;
        worst_power = worst_power.max(f64::from((reference.total_power() - port.total_power()).abs()));

        // Below the floor, the original keeps its old list, which the floor test looks at.
        if reference.total_power() < power_floor {
            continue;
        }

        let mut reference_count = 0;
        for p in 0..100 {
            let (frequency, amplitude) = reference.peak(p);
            if amplitude == 0.0 {
                break;
            }
            reference_count += 1;

            if let Some(peak) = port.peaks().get(p) {
                worst_frequency = worst_frequency.max(f64::from((frequency - peak.frequency).abs()));
                worst_amplitude = worst_amplitude.max(f64::from((amplitude - peak.amplitude).abs()));
                peaks_compared += 1;
            }
        }
        if reference_count != port.peaks().len() {
            count_mismatches += 1;
        }
    }

    println!(
        "  {name}: {frames} frames, {peaks_compared} peaks, worst {worst_frequency:.5} Hz, amplitude {worst_amplitude:.7}, power {worst_power:.5}, {count_mismatches} frames with a different count"
    );
    assert!(peaks_compared >= frames, "{name} has peaks to compare");
    assert!(
        count_mismatches == 0 && worst_frequency < 0.005 && worst_amplitude < 5e-6 && worst_power < 5e-4,
        "{name} matches the original"
    );
}

#[test]
fn fiddle_match() {
    const LENGTH: usize = 96000;
    let compare = |name, signal: &[f32]| compare_with_original(name, signal, WINDOW, SAMPLE_RATE, 40.0);

    compare("a single sine", &make_tones(&[tone(440.0, 0.5)], LENGTH, SAMPLE_RATE));
    compare(
        "five inharmonic sines",
        &make_tones(
            &[
                tone(233.0, 0.4),
                tone(587.0, 0.2),
                tone(1234.0, 0.1),
                tone(3100.0, 0.05),
                tone(7001.0, 0.02),
            ],
            LENGTH,
            SAMPLE_RATE,
        ),
    );

    let harmonics: Vec<Tone> = (1..=40)
        .map(|h| tone(110.0 * f64::from(h), 0.4 / f64::from(h)))
        .collect();
    compare("a sawtooth-like tone", &make_tones(&harmonics, LENGTH, SAMPLE_RATE));

    compare(
        "tones in noise",
        &add(
            &make_tones(&[tone(330.0, 0.3), tone(1500.0, 0.2)], LENGTH, SAMPLE_RATE),
            &make_noise(0.003, LENGTH),
        ),
    );

    let mut phase = 0.0;
    let sweep: Vec<f32> = (0..LENGTH)
        .map(|i| {
            phase += 2.0 * 3.14159265358979 * (100.0 * 2f64.powf(5.0 * i as f64 / LENGTH as f64)) / SAMPLE_RATE;
            (0.5 * phase.sin()) as f32
        })
        .collect();
    compare("a sweep", &sweep);

    compare(
        "onsets out of silence",
        &concatenate(
            &vec![0.0; 5000],
            &make_tones(&[tone(700.0, 0.5), tone(1300.0, 0.25)], LENGTH, SAMPLE_RATE),
        ),
    );

    compare_with_original(
        "another window size",
        &make_tones(&[tone(440.0, 0.5), tone(1234.0, 0.1)], LENGTH, 96000.0),
        4096,
        96000.0,
        40.0,
    );
    compare_with_original(
        "a smaller window",
        &make_tones(&[tone(700.0, 0.5)], LENGTH, 44100.0),
        1024,
        44100.0,
        40.0,
    );
}

#[test]
fn fiddle_accuracy() {
    for frequency in [110.0, 440.3, 1000.0, 5555.5, 15000.0] {
        let mut port = analyzer(WINDOW, SAMPLE_RATE);
        run_through(&mut port, &make_tones(&[tone(frequency, 0.5)], 9 * HOP, SAMPLE_RATE));

        let strongest = strongest(&port);
        println!(
            "  {frequency:.1} Hz at 0.5 reads as {:.3} Hz at {:.4}",
            strongest.frequency, strongest.amplitude
        );
        assert!(
            (f64::from(strongest.frequency) - frequency).abs() < frequency * 0.001,
            "the frequency is right to within 0.1%"
        );
        assert!(
            (strongest.amplitude - 0.5).abs() < 0.025,
            "the amplitude is right to within 5%"
        );
    }
}

#[test]
fn fiddle_floor() {
    const FLOOR: f32 = 40.0;
    let loud = make_tones(&[tone(440.0, 0.5)], 6 * HOP, SAMPLE_RATE);
    let silence = vec![0f32; 6 * HOP];

    let mut port = analyzer(WINDOW, SAMPLE_RATE);
    port.set_power_floor(FLOOR);
    run_through(&mut port, &loud);
    assert!(!port.peaks().is_empty(), "the tone has a peak");
    run_through(&mut port, &silence);
    assert!(port.peaks().is_empty(), "in silence, the port has no peaks");

    // This documents the difference from the original, and why there is one: its list goes on showing the tone.
    let mut reference = ReferenceFiddle::new(WINDOW, SAMPLE_RATE, FLOOR);
    for block in concatenate(&loud, &silence).chunks_exact(REFERENCE_BLOCK) {
        reference.process(block);
    }
    let (frequency, amplitude) = reference.peak(0);
    println!("  in silence, the original still reports {frequency:.1} Hz at {amplitude:.3}");
    // The last frame above the floor straddles the end of the tone, so this is about half the tone's level.
    assert!(amplitude > 0.1, "the original keeps reporting the old peak");
}

#[test]
fn fiddle_power() {
    for window in [2048, 4096, 8192] {
        let mut port = analyzer(window, SAMPLE_RATE);
        run_through(&mut port, &make_tones(&[tone(1000.0, 1.0)], window * 3, SAMPLE_RATE));
        let difference = port.total_power() - FiddleAnalyzer::full_scale_power(window);
        println!(
            "  window {window}: a full-scale sine reads {:.2}, against {:.2}",
            port.total_power(),
            FiddleAnalyzer::full_scale_power(window)
        );
        assert!(
            difference.abs() < 0.5,
            "the conversion to dB below full scale is right to within 0.5 dB"
        );
    }
}

#[test]
fn fiddle_sample_rates() {
    for (sample_rate, window) in [(44100.0, 2048), (96000.0, 4096), (192000.0, 8192)] {
        let mut port = analyzer(window, sample_rate);
        run_through(&mut port, &make_tones(&[tone(1000.0, 0.5)], window * 4, sample_rate));
        let strongest = strongest(&port);
        println!("  {sample_rate:.0} Hz: 1000 Hz reads as {:.3} Hz", strongest.frequency);
        assert!((strongest.frequency - 1000.0).abs() < 1.0, "the frequency is right");
    }
}

fn peaks_of(list: &[(f32, f32)]) -> Vec<SpectralPeak> {
    list.iter()
        .map(|&(frequency, amplitude)| SpectralPeak { frequency, amplitude })
        .collect()
}

fn feed(tracker: &mut PartialTracker, peaks: &[SpectralPeak]) -> Targets {
    *tracker.update(peaks)
}

fn voice_at(targets: &Targets, frequency: f32) -> Option<usize> {
    targets
        .iter()
        .position(|target| target.amplitude > 0.0 && (target.frequency - frequency).abs() < 0.5)
}

#[test]
fn tracker_loudest() {
    let mut tracker = PartialTracker::default();
    let targets = feed(
        &mut tracker,
        &peaks_of(&[
            (100.0, 0.01),
            (200.0, 0.5),
            (300.0, 0.02),
            (400.0, 0.4),
            (500.0, 0.3),
            (600.0, 0.2),
            (700.0, 0.1),
        ]),
    );

    for loud in [200.0, 400.0, 500.0, 600.0, 700.0] {
        assert!(voice_at(&targets, loud).is_some(), "the loudest are played");
    }
    assert!(
        voice_at(&targets, 100.0).is_none() && voice_at(&targets, 300.0).is_none(),
        "the quieter ones aren't"
    );
    assert!(
        targets.iter().all(|target| target.is_new),
        "every voice starting is marked as new"
    );
}

#[test]
fn tracker_continuity() {
    let mut tracker = PartialTracker::default();
    let first = feed(&mut tracker, &peaks_of(&[(440.0, 0.5), (880.0, 0.25)]));
    let voice440 = voice_at(&first, 440.0).unwrap();
    let voice880 = voice_at(&first, 880.0).unwrap();

    // A lower peak arrives. In the original's list, this would move the other two down a place.
    let second = feed(&mut tracker, &peaks_of(&[(220.0, 0.3), (440.0, 0.5), (880.0, 0.25)]));
    assert_eq!(
        voice_at(&second, 440.0),
        Some(voice440),
        "the voice at 440 Hz stays on it"
    );
    assert_eq!(
        voice_at(&second, 880.0),
        Some(voice880),
        "the voice at 880 Hz stays on it"
    );
    assert!(!second[voice440].is_new && !second[voice880].is_new, "those aren't new");
    let voice220 = voice_at(&second, 220.0).expect("the lower one has its own voice");
    assert!(voice220 != voice440 && voice220 != voice880);
    assert!(second[voice220].is_new, "that one is new");

    // A slide, within what still counts as the same partial.
    let third = feed(&mut tracker, &peaks_of(&[(220.0, 0.3), (450.0, 0.5), (880.0, 0.25)]));
    assert_eq!(
        voice_at(&third, 450.0),
        Some(voice440),
        "a voice follows its partial as it moves"
    );

    // Crossing paths doesn't swap voices when each still has a closer peak.
    let fourth = feed(&mut tracker, &peaks_of(&[(222.0, 0.3), (452.0, 0.5), (884.0, 0.25)]));
    assert_eq!(
        voice_at(&fourth, 452.0),
        Some(voice440),
        "each voice takes the peak closest to it"
    );
    assert_eq!(voice_at(&fourth, 884.0), Some(voice880));
}

#[test]
fn tracker_fade() {
    let mut tracker = PartialTracker::default();
    let first = feed(&mut tracker, &peaks_of(&[(440.0, 0.5)]));
    let voice = voice_at(&first, 440.0).unwrap();

    let second = feed(&mut tracker, &[]);
    assert_eq!(
        second[voice].amplitude, 0.0,
        "with no peak, the voice's level goes to zero"
    );
    assert_eq!(second[voice].frequency, 440.0, "and it keeps its pitch as it fades");
}

#[test]
fn tracker_new_waits() {
    let mut tracker = PartialTracker::default();
    let first = feed(
        &mut tracker,
        &peaks_of(&[(100.0, 0.5), (200.0, 0.5), (300.0, 0.5), (400.0, 0.5), (500.0, 0.5)]),
    );
    assert!(
        first.iter().all(|target| target.amplitude > 0.0),
        "all the voices are playing"
    );

    // Five different partials, too far away to be the same ones.
    let next = peaks_of(&[(150.0, 0.5), (250.0, 0.5), (350.0, 0.5), (450.0, 0.5), (550.0, 0.5)]);
    let second = feed(&mut tracker, &next);
    for v in 0..VOICE_COUNT {
        assert!(
            second[v].amplitude == 0.0 && !second[v].is_new,
            "a voice being heard doesn't change pitch: it fades"
        );
        assert_eq!(
            second[v].frequency, first[v].frequency,
            "and keeps its pitch while it does"
        );
    }

    let third = feed(&mut tracker, &next);
    for frequency in [150.0, 250.0, 350.0, 450.0, 550.0] {
        assert!(
            voice_at(&third, frequency).is_some(),
            "then the new partials start, once the voices are silent"
        );
    }
}

#[test]
fn tracker_keeps_place() {
    let mut tracker = PartialTracker::default();
    feed(
        &mut tracker,
        &peaks_of(&[(100.0, 0.5), (200.0, 0.5), (300.0, 0.5), (400.0, 0.5), (500.0, 0.100)]),
    );

    // A sixth peak, slightly louder than the fifth one and then slightly quieter, in turn.
    let mut changes = 0;
    for frame in 0..20 {
        let a = if frame % 2 == 0 { 0.110 } else { 0.095 };
        let targets = feed(
            &mut tracker,
            &peaks_of(&[
                (100.0, 0.5),
                (200.0, 0.5),
                (300.0, 0.5),
                (400.0, 0.5),
                (500.0, 0.100),
                (600.0, a),
            ]),
        );
        if voice_at(&targets, 500.0).is_none() {
            changes += 1;
        }
    }
    assert_eq!(
        changes, 0,
        "a peak being played isn't displaced by one that's about the same level"
    );

    let louder = peaks_of(&[
        (100.0, 0.5),
        (200.0, 0.5),
        (300.0, 0.5),
        (400.0, 0.5),
        (500.0, 0.1),
        (600.0, 0.3),
    ]);
    let targets = feed(&mut tracker, &louder);
    assert!(voice_at(&targets, 500.0).is_none(), "a clearly louder one does");
    let targets = feed(&mut tracker, &louder);
    assert!(
        voice_at(&targets, 600.0).is_some(),
        "and gets the voice in the next frame"
    );
}
