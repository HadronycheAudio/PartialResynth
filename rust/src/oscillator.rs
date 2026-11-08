//! A band-limited oscillator.

use hadronyche_dsp::step_correction;

/// The order matches WAVES in lib.rs, and projects save a position in it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Wave {
    Sine,
    Saw,
    Square,
}

impl Wave {
    pub fn from_index(index: i32) -> Self {
        match index {
            1 => Wave::Saw,
            2 => Wave::Square,
            _ => Wave::Sine,
        }
    }
}

const TWO_PI: f64 = core::f64::consts::TAU;

/// Above this, steps come too close together for the correction to deal with them. A saw or square this high is
/// nearly a sine anyway, so fading it out only loses a little at the very top.
const FADE_START: f64 = 0.2;
const FADE_END: f64 = 0.25;

/// The saw and square use a 4-point polynomial correction around each step.
#[derive(Clone, Debug, Default)]
pub struct Oscillator {
    phase: f64,
}

impl Oscillator {
    pub fn reset(&mut self) {
        self.phase = 0.0;
    }

    /// `increment` is the frequency in cycles per sample.
    pub fn process(&mut self, wave: Wave, increment: f64) -> f32 {
        self.phase += increment;
        if self.phase >= 1.0 {
            self.phase -= self.phase.floor();
        }

        if wave == Wave::Sine {
            return if increment < 0.5 {
                (TWO_PI * self.phase).sin() as f32
            } else {
                0.0
            };
        }

        if increment >= FADE_END {
            return 0.0;
        }

        let mut value = if wave == Wave::Saw {
            self.saw(increment)
        } else {
            self.square(increment)
        };
        if increment > FADE_START {
            value *= (FADE_END - increment) / (FADE_END - FADE_START);
        }
        value as f32
    }

    /// The correction at the wrap, which is a step of +1 in units of its size.
    fn wrap_correction(&self, increment: f64) -> f64 {
        let reach = 2.0 * increment;
        let mut correction = 0.0;
        if self.phase < reach {
            correction += step_correction(self.phase / increment);
        }
        if 1.0 - self.phase < reach {
            correction += step_correction((self.phase - 1.0) / increment);
        }
        correction
    }

    fn saw(&self, increment: f64) -> f64 {
        2.0 * self.phase - 1.0 - 2.0 * self.wrap_correction(increment)
    }

    fn square(&self, increment: f64) -> f64 {
        let mut value = (if self.phase < 0.5 { 1.0 } else { -1.0 }) + 2.0 * self.wrap_correction(increment);

        let from_middle = self.phase - 0.5;
        if from_middle.abs() < 2.0 * increment {
            value -= 2.0 * step_correction(from_middle / increment);
        }
        value
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_sine_is_a_sine_and_stops_above_nyquist() {
        let mut osc = Oscillator::default();
        assert!((osc.process(Wave::Sine, 0.25) - 1.0).abs() < 1e-6);
        assert_eq!(osc.process(Wave::Sine, 0.5), 0.0);
    }

    #[test]
    fn the_waves_are_numbered_as_the_list_is() {
        assert_eq!(Wave::from_index(0), Wave::Sine);
        assert_eq!(Wave::from_index(1), Wave::Saw);
        assert_eq!(Wave::from_index(2), Wave::Square);
        assert_eq!(Wave::from_index(7), Wave::Sine);
    }
}
