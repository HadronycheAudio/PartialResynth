//! One partial's synth voice: two oscillators into a filter, then the level from the analysis.

use hadronyche_dsp::{SvfMode, pow2};

use crate::oscillator::{Oscillator, Wave};
use crate::partial_tracker::Target;
use crate::state_variable_filter::StateVariableFilter;

/// What every voice shares, worked out once per control tick. One set of controls plays all the voices.
#[derive(Clone, Copy, Debug)]
pub struct VoiceControls {
    pub wave1: Wave,
    pub wave2: Wave,
    pub level1: f32,
    pub level2: f32,
    /// The oscillators' tuning, as a multiple of the partial's frequency.
    pub ratio1: f64,
    pub ratio2: f64,

    pub filter_mode: SvfMode,
    /// log2 of the frequency, so that the modulation below just adds to it.
    pub cutoff_octaves: f64,
    pub q: f64,
    pub key_follow: f64,
    pub env_amount: f64,

    /// Per control tick. Zero means no glide.
    pub glide_coefficient: f64,
}

impl Default for VoiceControls {
    fn default() -> Self {
        Self {
            wave1: Wave::Sine,
            wave2: Wave::Saw,
            level1: 1.0,
            level2: 0.0,
            ratio1: 1.0,
            ratio2: 1.0,
            filter_mode: SvfMode::LowPass,
            cutoff_octaves: 0.0,
            q: 0.707,
            key_follow: 0.0,
            env_amount: 0.0,
            glide_coefficient: 0.0,
        }
    }
}

const ENVELOPE_RANGE_DB: f64 = 60.0;
/// log2 (440), as the C++ wrote it.
#[allow(clippy::excessive_precision)]
const KEY_FOLLOW_CENTRE_OCTAVES: f64 = 8.781359713524660;
const LEVEL_OFF: f32 = 1e-5;
/// A frame arrives every quarter of a window.
const FRAMES_TO_FILL_WINDOW: i32 = 4;

/// The level comes last so that the filter has no tail: when the partial's gone, so is the voice's sound.
#[derive(Clone, Debug)]
pub struct Voice {
    sample_rate: f64,
    ramp_samples: i32,

    oscillator1: Oscillator,
    oscillator2: Oscillator,
    filter: StateVariableFilter,

    amplitude: f32,
    amplitude_step: f32,
    amplitude_target: f32,
    /// log2 of the frequency in Hz.
    pitch: f64,
    pitch_step: f64,
    /// `pitch` after the glide.
    gliding: f64,
    frames_before_glide: i32,
    ramp_left: i32,

    increment1: f64,
    increment2: f64,
}

impl Default for Voice {
    fn default() -> Self {
        Self {
            sample_rate: 44100.0,
            ramp_samples: 1,
            oscillator1: Oscillator::default(),
            oscillator2: Oscillator::default(),
            filter: StateVariableFilter::default(),
            amplitude: 0.0,
            amplitude_step: 0.0,
            amplitude_target: 0.0,
            pitch: 0.0,
            pitch_step: 0.0,
            gliding: 0.0,
            frames_before_glide: 0,
            ramp_left: 0,
            increment1: 0.0,
            increment2: 0.0,
        }
    }
}

impl Voice {
    pub fn prepare(&mut self, sample_rate: f64, ramp_samples: i32) {
        self.sample_rate = sample_rate;
        self.ramp_samples = ramp_samples;
        self.filter.prepare(sample_rate);
        self.oscillator1.reset();
        self.oscillator2.reset();
        self.amplitude = 0.0;
        self.amplitude_step = 0.0;
        self.pitch = 0.0;
        self.pitch_step = 0.0;
        self.gliding = 0.0;
        self.frames_before_glide = 0;
        self.ramp_left = 0;
    }

    /// From the analysis, at the start of a ramp lasting until the next frame's values arrive. Amplitude ramps
    /// linearly, as the envelope, and pitch ramps in octaves.
    pub fn set_target(&mut self, target: &Target, controls: &VoiceControls) {
        if target.amplitude == 0.0 && self.is_silent() {
            return;
        }

        self.amplitude_step = (target.amplitude - self.amplitude) / self.ramp_samples as f32;
        self.amplitude_target = target.amplitude;

        if target.frequency > 0.0 {
            let pitch = f64::from(target.frequency).log2();
            if target.is_new {
                // Nothing can be heard from before this, so it starts on its pitch, not gliding to it.
                self.pitch = pitch;
                self.pitch_step = 0.0;
                self.gliding = pitch;
                self.frames_before_glide = FRAMES_TO_FILL_WINDOW;
                self.filter.reset();
            } else {
                self.pitch_step = (pitch - self.pitch) / f64::from(self.ramp_samples);
                self.frames_before_glide = (self.frames_before_glide - 1).max(0);
            }
        }
        self.ramp_left = self.ramp_samples;

        // So that its first samples don't use what was worked out for the partial it played before.
        if target.is_new {
            self.update_controls(controls);
        }
    }

    pub fn is_silent(&self) -> bool {
        self.amplitude == 0.0 && self.ramp_left == 0
    }

    /// A coarser rate than the audio, for what's costly to calculate and can't be heard moving at this speed.
    pub fn update_controls(&mut self, controls: &VoiceControls) {
        if self.is_silent() {
            return;
        }

        // A partial's first frames see it through a part-filled window, which puts its pitch slightly off. A long
        // glide would hold on to that, so it starts once the window is full of the partial.
        self.gliding = if self.frames_before_glide > 0 {
            self.pitch
        } else {
            self.pitch + (self.gliding - self.pitch) * controls.glide_coefficient
        };

        let frequency = pow2(self.gliding) / self.sample_rate;
        self.increment1 = frequency * controls.ratio1;
        self.increment2 = frequency * controls.ratio2;

        // In terms of level, so that a quiet partial moves the filter as well, rather than hardly at all. The log is
        // taken in f32, as the C++'s log10f was.
        let level_db = f64::from(max(self.amplitude, 1e-6).log10());
        let envelope = (1.0 + 20.0 * level_db / ENVELOPE_RANGE_DB).clamp(0.0, 1.0);

        let octaves = controls.cutoff_octaves
            + controls.key_follow * (self.gliding - KEY_FOLLOW_CENTRE_OCTAVES)
            + controls.env_amount * envelope;
        self.filter.set_coefficients(pow2(octaves), controls.q);
    }

    pub fn process(&mut self, controls: &VoiceControls) -> f32 {
        if self.is_silent() {
            return 0.0;
        }

        let mut sum = 0f32;
        if controls.level1 > LEVEL_OFF {
            sum += controls.level1 * self.oscillator1.process(controls.wave1, self.increment1);
        }
        if controls.level2 > LEVEL_OFF {
            sum += controls.level2 * self.oscillator2.process(controls.wave2, self.increment2);
        }

        let output = self.filter.process(sum, controls.filter_mode) * self.amplitude;

        if self.ramp_left > 0 {
            self.amplitude += self.amplitude_step;
            self.pitch += self.pitch_step;
            self.ramp_left -= 1;
            if self.ramp_left == 0 {
                // Exact, because adding up steps wouldn't reach zero exactly.
                self.amplitude = self.amplitude_target;
            }
        }
        output
    }
}

/// `std::max` on floats: the second wins only when it's greater.
fn max(a: f32, b: f32) -> f32 {
    if a < b { b } else { a }
}
