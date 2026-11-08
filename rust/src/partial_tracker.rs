//! Chooses which peaks to play and which voice plays each of them.

use crate::fiddle_analyzer::{MAX_PEAKS, SpectralPeak};

pub const VOICE_COUNT: usize = 5;

/// What one voice should play from the next frame on.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Target {
    pub frequency: f32,
    pub amplitude: f32,
    /// This voice was silent, and starts a different partial.
    pub is_new: bool,
}

pub type Targets = [Target; VOICE_COUNT];

/// Beyond this between frames, a peak counts as a different partial. It's about what vibrato and a fast slide cover
/// in a frame. Past that, the old partial fades as the new one comes in.
const MAX_JUMP_SEMITONES: f32 = 1.0;

/// A peak that a voice is already playing needs to be beaten by this much (3 dB) to lose its place. Without it, two
/// peaks of about the same level, with one place between them, would take turns in every frame.
const KEEP_ADVANTAGE: f32 = 1.41;

/// 12 / ln (2), as the C++ wrote it.
#[allow(clippy::excessive_precision)]
fn semitones_between(a: f32, b: f32) -> f32 {
    (17.3123405 * (a / b).ln()).abs()
}

/// A voice follows one partial through time rather than the position that partial happens to have in a frame's list
/// of peaks.
#[derive(Clone, Debug)]
pub struct PartialTracker {
    targets: Targets,
    scores: [f32; MAX_PEAKS],
    order: [usize; MAX_PEAKS],
}

impl Default for PartialTracker {
    fn default() -> Self {
        Self {
            targets: Targets::default(),
            scores: [0.0; MAX_PEAKS],
            order: [0; MAX_PEAKS],
        }
    }
}

impl PartialTracker {
    pub fn reset(&mut self) {
        self.targets = Targets::default();
    }

    pub fn update(&mut self, peaks: &[SpectralPeak]) -> &Targets {
        let chosen_count = self.choose_loudest(peaks);

        let mut is_voice_taken = [false; VOICE_COUNT];
        let mut is_peak_taken = [false; VOICE_COUNT];
        self.continue_partials(peaks, chosen_count, &mut is_voice_taken, &mut is_peak_taken);

        // A voice with no partial left fades out, and keeps its pitch while it does.
        let mut is_fading_out = [false; VOICE_COUNT];
        for (v, target) in self.targets.iter_mut().enumerate() {
            target.is_new = false;
            is_fading_out[v] = !is_voice_taken[v] && target.amplitude > 0.0;
            if !is_voice_taken[v] {
                target.amplitude = 0.0;
            }
        }

        self.start_partials(peaks, chosen_count, &is_voice_taken, &is_fading_out, &is_peak_taken);
        &self.targets
    }

    fn is_being_played(&self, frequency: f32) -> bool {
        self.targets.iter().any(|target| {
            target.amplitude > 0.0 && semitones_between(target.frequency, frequency) <= MAX_JUMP_SEMITONES
        })
    }

    /// Puts the chosen peaks' places in `order`, loudest first, with the advantage for one that's being played.
    fn choose_loudest(&mut self, peaks: &[SpectralPeak]) -> usize {
        for (p, peak) in peaks.iter().enumerate() {
            self.scores[p] = peak.amplitude
                * if self.is_being_played(peak.frequency) {
                    KEEP_ADVANTAGE
                } else {
                    1.0
                };
            self.order[p] = p;
        }

        // Loudest first, and of two equal scores the lower in frequency, which the C++ left to its library's heap.
        let scores = &self.scores;
        self.order[..peaks.len()]
            .sort_by(|&a, &b| scores[b].partial_cmp(&scores[a]).unwrap_or(core::cmp::Ordering::Equal));
        peaks.len().min(VOICE_COUNT)
    }

    /// Repeatedly takes the closest pair of a sounding voice and a chosen peak.
    fn continue_partials(
        &mut self,
        peaks: &[SpectralPeak],
        chosen_count: usize,
        is_voice_taken: &mut [bool; VOICE_COUNT],
        is_peak_taken: &mut [bool; VOICE_COUNT],
    ) {
        loop {
            let mut closest = MAX_JUMP_SEMITONES;
            let mut best: Option<(usize, usize)> = None;

            for (v, target) in self.targets.iter().enumerate() {
                if is_voice_taken[v] || target.amplitude <= 0.0 {
                    continue;
                }

                for c in 0..chosen_count {
                    if is_peak_taken[c] {
                        continue;
                    }

                    let distance = semitones_between(target.frequency, peaks[self.order[c]].frequency);
                    if distance <= closest {
                        closest = distance;
                        best = Some((v, c));
                    }
                }
            }

            let Some((best_voice, best_chosen)) = best else {
                return;
            };

            let peak = peaks[self.order[best_chosen]];
            self.targets[best_voice].frequency = peak.frequency;
            self.targets[best_voice].amplitude = peak.amplitude;
            is_voice_taken[best_voice] = true;
            is_peak_taken[best_chosen] = true;
        }
    }

    /// Only a silent voice takes on a new partial, because changing a pitch that can be heard would be a jump. A
    /// voice that's fading out finishes before the next frame, so a peak with nowhere to go now gets its place then.
    fn start_partials(
        &mut self,
        peaks: &[SpectralPeak],
        chosen_count: usize,
        is_voice_taken: &[bool; VOICE_COUNT],
        is_fading_out: &[bool; VOICE_COUNT],
        is_peak_taken: &[bool; VOICE_COUNT],
    ) {
        let mut voice = 0;
        for c in 0..chosen_count {
            if is_peak_taken[c] {
                continue;
            }

            while voice < VOICE_COUNT && (is_voice_taken[voice] || is_fading_out[voice]) {
                voice += 1;
            }
            if voice == VOICE_COUNT {
                return;
            }

            let peak = peaks[self.order[c]];
            let target = &mut self.targets[voice];
            target.frequency = peak.frequency;
            target.amplitude = peak.amplitude;
            target.is_new = true;
            voice += 1;
        }
    }
}
