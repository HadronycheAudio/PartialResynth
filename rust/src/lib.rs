//! PartialResynth's signal processing: the parameters, and the processor that runs the signal path described in
//! docs/signal-path.md. The C++ side, in src/, is the VST3 classes around it, and reads the parameter table from
//! here through `hadronyche_plugin`.

pub mod fiddle_analyzer;
pub mod partial_tracker;

mod oscillator;
mod state_variable_filter;
mod voice;

use core::ffi::CStr;

use hadronyche_dsp::{Smoother, VOLUME_SILENCE_DB, dry_gain, pow2, volume_to_gain, wet_gain};
use hadronyche_plugin::{Block, Curve, Output, ParamSpec, Params, Processor, Tail, export_plugin, params};

use fiddle_analyzer::FiddleAnalyzer;
use oscillator::Wave;
use partial_tracker::{PartialTracker, VOICE_COUNT};
use state_variable_filter::mode_from_index;
use voice::{Voice, VoiceControls};

// Saved in projects by position, so only ever add to the end of these. The order of WAVES matches `Wave`, and the
// order of FILTER_MODES matches `SvfMode`.
const WAVES: &[&CStr] = &[c"Sine", c"Saw", c"Square"];
const FILTER_MODES: &[&CStr] = &[c"Low-pass", c"High-pass", c"Band-pass", c"Notch"];

params! {
    /// Parameter IDs are saved in projects and automation: never reorder or remove one, only add at the end.
    pub enum Param => PARAMS {
        Threshold => ParamSpec::new(c"Threshold", c"dB", -90.0, -20.0, -60.0, Curve::Linear, 0),
        Glide => ParamSpec::new(c"Glide", c"ms", 0.0, 2000.0, 0.0, Curve::Squared, 0),

        Osc1Wave => ParamSpec::list(c"Osc 1 wave", WAVES, 0),
        Osc1Level => ParamSpec::new(c"Osc 1 level", c"%", 0.0, 100.0, 100.0, Curve::Linear, 0),
        Osc1Coarse => ParamSpec::new(c"Osc 1 coarse", c"st", -24.0, 24.0, 0.0, Curve::Integer, 0),
        Osc1Fine => ParamSpec::new(c"Osc 1 fine", c"ct", -100.0, 100.0, 0.0, Curve::Linear, 0),

        Osc2Wave => ParamSpec::list(c"Osc 2 wave", WAVES, 1),
        Osc2Level => ParamSpec::new(c"Osc 2 level", c"%", 0.0, 100.0, 0.0, Curve::Linear, 0),
        Osc2Coarse => ParamSpec::new(c"Osc 2 coarse", c"st", -24.0, 24.0, 0.0, Curve::Integer, 0),
        Osc2Fine => ParamSpec::new(c"Osc 2 fine", c"ct", -100.0, 100.0, 0.0, Curve::Linear, 0),

        FilterMode => ParamSpec::list(c"Filter mode", FILTER_MODES, 0),
        FilterCutoff => ParamSpec::new(c"Filter cutoff", c"Hz", 20.0, 20000.0, 20000.0, Curve::Log, 0),
        FilterResonance => ParamSpec::new(c"Filter resonance", c"%", 0.0, 100.0, 0.0, Curve::Linear, 0),
        FilterKeyFollow => ParamSpec::new(c"Filter key follow", c"%", 0.0, 100.0, 0.0, Curve::Linear, 0),
        FilterEnvAmount => ParamSpec::new(c"Filter env amount", c"oct", -8.0, 8.0, 0.0, Curve::Linear, 2),

        Spread => ParamSpec::new(c"Spread", c"%", 0.0, 100.0, 0.0, Curve::Linear, 0),
        DryWet => ParamSpec::new(c"Dry/wet", c"%", 0.0, 100.0, 100.0, Curve::Linear, 0),
        Volume => ParamSpec::new(c"Volume", c"dB", VOLUME_SILENCE_DB, 6.0, 0.0, Curve::Linear, 1).silent_at_min(),
    }
}

const SMOOTHING_MS: f64 = 20.0;

/// How often the voices update what's costly to calculate, in samples.
const CONTROL_INTERVAL: i32 = 16;

/// The glide's time is how long it takes to cover 95% of the interval, which is 3 time constants.
const GLIDE_TIME_CONSTANTS: f64 = 3.0;

/// Below this, a block of input counts as silent. It's about -140 dB.
const SILENT_PEAK: f32 = 1e-7;

/// Resonance goes from no peak at all to a strong one, evenly in terms of Q.
const MIN_Q: f64 = 0.707;
const MAX_Q: f64 = 20.0;

/// Across the stereo field when Spread is at 100%. The first voice is centred.
const PAN_POSITIONS: [f64; VOICE_COUNT] = [0.0, -0.5, 0.5, -1.0, 1.0];

/// Doubling the sample rate doubles the window, which keeps its length in time, and so what it can tell apart.
pub fn window_size_for(sample_rate: f64) -> usize {
    if sample_rate > 128000.0 {
        return 8192;
    }
    if sample_rate > 64000.0 { 4096 } else { 2048 }
}

/// Half the window, because a window's result describes what happened around its centre, then the hop, because the
/// voices take that long to reach it. The hop is a quarter window, with the two analyzers staggered.
pub fn latency_for(window_size: usize) -> usize {
    window_size / 2 + window_size / 4
}

fn q_from_resonance(percent: f64) -> f64 {
    MIN_Q * (MAX_Q / MIN_Q).powf(percent / 100.0)
}

pub struct PartialResynth {
    sample_rate: f64,
    window_size: usize,

    /// Two copies of fiddle~, half a hop apart, so that together they give a frame every quarter window.
    analyzers: [FiddleAnalyzer; 2],
    tracker: PartialTracker,
    voices: [Voice; VOICE_COUNT],
    voice_controls: VoiceControls,
    pan_left: [f32; VOICE_COUNT],
    pan_right: [f32; VOICE_COUNT],
    control_countdown: i32,

    // Per control tick.
    level1: Smoother,
    level2: Smoother,
    /// In semitones.
    tuning1: Smoother,
    tuning2: Smoother,
    cutoff_octaves: Smoother,
    q: Smoother,
    key_follow: Smoother,
    env_amount: Smoother,
    spread: Smoother,

    // Per sample.
    dry_gain: Smoother,
    wet_gain: Smoother,
    volume: Smoother,

    /// Delays the dry signal by the plug-in's latency, so that it lines up with the voices.
    dry_delay: [Vec<f32>; 2],
    dry_position: usize,

    /// With nothing left to hear, and nothing left in the buffers, the plug-in stops working.
    silent_input_samples: i64,
    is_first_block: bool,
}

impl PartialResynth {
    fn apply_parameters(&mut self, params: &Params<'_>) {
        let power_floor = params.plain(Param::Threshold) as f32 + FiddleAnalyzer::full_scale_power(self.window_size);
        for analyzer in &mut self.analyzers {
            analyzer.set_power_floor(power_floor);
        }

        let ticks_per_second = self.sample_rate / f64::from(CONTROL_INTERVAL);
        let glide_seconds = params.plain(Param::Glide) / 1000.0 / GLIDE_TIME_CONSTANTS;
        self.voice_controls.glide_coefficient = if glide_seconds > 0.0 {
            (-1.0 / (ticks_per_second * glide_seconds)).exp()
        } else {
            0.0
        };

        self.voice_controls.wave1 = Wave::from_index(params.plain(Param::Osc1Wave) as i32);
        self.voice_controls.wave2 = Wave::from_index(params.plain(Param::Osc2Wave) as i32);
        self.voice_controls.filter_mode = mode_from_index(params.plain(Param::FilterMode) as i32);

        self.level1.set_target(params.plain(Param::Osc1Level) / 100.0);
        self.level2.set_target(params.plain(Param::Osc2Level) / 100.0);
        self.tuning1
            .set_target(params.plain(Param::Osc1Coarse) + params.plain(Param::Osc1Fine) / 100.0);
        self.tuning2
            .set_target(params.plain(Param::Osc2Coarse) + params.plain(Param::Osc2Fine) / 100.0);
        self.cutoff_octaves.set_target(params.plain(Param::FilterCutoff).log2());
        self.q
            .set_target(q_from_resonance(params.plain(Param::FilterResonance)));
        self.key_follow.set_target(params.plain(Param::FilterKeyFollow) / 100.0);
        self.env_amount.set_target(params.plain(Param::FilterEnvAmount));
        self.spread.set_target(params.plain(Param::Spread) / 100.0);
        self.dry_gain.set_target(dry_gain(params.plain(Param::DryWet)));
        self.wet_gain.set_target(wet_gain(params.plain(Param::DryWet)));
        self.volume.set_target(volume_to_gain(params.plain(Param::Volume)));
    }

    fn smoothers(&mut self) -> [&mut Smoother; 12] {
        [
            &mut self.level1,
            &mut self.level2,
            &mut self.tuning1,
            &mut self.tuning2,
            &mut self.cutoff_octaves,
            &mut self.q,
            &mut self.key_follow,
            &mut self.env_amount,
            &mut self.spread,
            &mut self.dry_gain,
            &mut self.wet_gain,
            &mut self.volume,
        ]
    }

    /// Moves everything straight to its current target, for when nobody can hear it moving.
    fn snap_smoothers(&mut self) {
        for smoother in self.smoothers() {
            smoother.snap_to_target();
        }
    }

    fn update_controls(&mut self) {
        self.voice_controls.level1 = self.level1.step() as f32;
        self.voice_controls.level2 = self.level2.step() as f32;
        self.voice_controls.ratio1 = pow2(self.tuning1.step() / 12.0);
        self.voice_controls.ratio2 = pow2(self.tuning2.step() / 12.0);
        self.voice_controls.cutoff_octaves = self.cutoff_octaves.step();
        self.voice_controls.q = self.q.step();
        self.voice_controls.key_follow = self.key_follow.step();
        self.voice_controls.env_amount = self.env_amount.step();

        // At constant power, with the gains scaled to reach 1 in the middle, so that with Spread at 0, the voices are
        // at the same level in both channels as they'd be in mono.
        let spread = self.spread.step();
        for (v, position) in PAN_POSITIONS.iter().enumerate() {
            let angle = core::f64::consts::FRAC_PI_4 * (1.0 + spread * position);
            self.pan_left[v] = (core::f64::consts::SQRT_2 * angle.cos()) as f32;
            self.pan_right[v] = (core::f64::consts::SQRT_2 * angle.sin()) as f32;
            self.voices[v].update_controls(&self.voice_controls);
        }
    }

    fn render(&mut self, input: Option<[&[f32]; 2]>, output: [&mut [f32]; 2]) {
        let delay_length = self.dry_delay[0].len();
        let [output_left, output_right] = output;

        for i in 0..output_left.len() {
            // A broken sample upstream would otherwise stay in the envelopes and filters until reactivation.
            let sample = |channel: usize| input.map_or(0.0, |input| input[channel][i]);
            let left = finite_or_silence(sample(0));
            let right = finite_or_silence(sample(1));

            let mono = 0.5 * (left + right);
            for analyzer in &mut self.analyzers {
                if analyzer.push(mono) {
                    let targets = self.tracker.update(analyzer.peaks());
                    for (voice, target) in self.voices.iter_mut().zip(targets) {
                        voice.set_target(target, &self.voice_controls);
                    }
                }
            }

            self.control_countdown -= 1;
            if self.control_countdown < 0 {
                self.control_countdown = CONTROL_INTERVAL - 1;
                self.update_controls();
            }

            let mut wet_left = 0f32;
            let mut wet_right = 0f32;
            for v in 0..VOICE_COUNT {
                let voice = self.voices[v].process(&self.voice_controls);
                wet_left += voice * self.pan_left[v];
                wet_right += voice * self.pan_right[v];
            }

            let dry_left = self.dry_delay[0][self.dry_position];
            let dry_right = self.dry_delay[1][self.dry_position];
            self.dry_delay[0][self.dry_position] = left;
            self.dry_delay[1][self.dry_position] = right;
            self.dry_position = if self.dry_position + 1 == delay_length {
                0
            } else {
                self.dry_position + 1
            };

            let dry_gain = self.dry_gain.step_f32();
            let wet_gain = self.wet_gain.step_f32();
            let volume = self.volume.step_f32();
            output_left[i] = (dry_left * dry_gain + wet_left * wet_gain) * volume;
            output_right[i] = (dry_right * dry_gain + wet_right * wet_gain) * volume;
        }
    }
}

fn finite_or_silence(sample: f32) -> f32 {
    if sample.is_finite() { sample } else { 0.0 }
}

/// `std::max` on floats: the second wins only when it's greater.
fn max(a: f32, b: f32) -> f32 {
    if a < b { b } else { a }
}

impl Processor for PartialResynth {
    const PARAMS: &'static [ParamSpec] = &PARAMS;
    const STATE_VERSION: i32 = 1;
    const HAS_AUDIO_INPUT: bool = true;

    fn new() -> Self {
        Self {
            sample_rate: 0.0,
            window_size: 2048,
            analyzers: [FiddleAnalyzer::default(), FiddleAnalyzer::default()],
            tracker: PartialTracker::default(),
            voices: Default::default(),
            voice_controls: VoiceControls::default(),
            pan_left: [0.0; VOICE_COUNT],
            pan_right: [0.0; VOICE_COUNT],
            control_countdown: 0,
            level1: Smoother::default(),
            level2: Smoother::default(),
            tuning1: Smoother::default(),
            tuning2: Smoother::default(),
            cutoff_octaves: Smoother::default(),
            q: Smoother::default(),
            key_follow: Smoother::default(),
            env_amount: Smoother::default(),
            spread: Smoother::default(),
            dry_gain: Smoother::default(),
            wet_gain: Smoother::default(),
            volume: Smoother::default(),
            dry_delay: [Vec::new(), Vec::new()],
            dry_position: 0,
            silent_input_samples: 0,
            is_first_block: true,
        }
    }

    fn prepare(&mut self, sample_rate: f64, _max_block_size: usize) {
        self.sample_rate = sample_rate;
        self.window_size = window_size_for(sample_rate);
        let hop = self.window_size / 4;

        self.analyzers[0].prepare(self.window_size, sample_rate, 0);
        self.analyzers[1].prepare(self.window_size, sample_rate, hop);
        self.tracker.reset();

        for voice in &mut self.voices {
            voice.prepare(sample_rate, hop as i32);
        }

        let ticks_per_second = sample_rate / f64::from(CONTROL_INTERVAL);
        let mut smoothers = self.smoothers();
        let (per_tick, per_sample) = smoothers.split_at_mut(9);
        for smoother in per_tick {
            smoother.prepare(ticks_per_second, SMOOTHING_MS);
        }
        for smoother in per_sample {
            smoother.prepare(sample_rate, SMOOTHING_MS);
        }

        for channel in &mut self.dry_delay {
            channel.clear();
            channel.resize(latency_for(self.window_size), 0.0);
        }
        self.dry_position = 0;

        self.control_countdown = 0;
        self.silent_input_samples = 0;
        self.is_first_block = true;

        // The parameters' defaults, until the first block brings the host's values.
        let defaults: Vec<f64> = PARAMS
            .iter()
            .map(|spec| spec.plain_to_normalized(spec.default))
            .collect();
        self.apply_parameters(&Params::new(&PARAMS, &defaults));
        self.snap_smoothers();
    }

    fn process(&mut self, block: Block<'_>) -> Output {
        self.apply_parameters(&block.params);

        // Changes arriving with the first block are where the plug-in starts, not something to ramp to.
        if self.is_first_block {
            self.snap_smoothers();
            self.is_first_block = false;
        }

        let count = block.len();

        // Looks at the samples rather than the host's silence flags, which are only ever a hint.
        let input_peak = block.input.map_or(0.0, |[left, right]| {
            left.iter()
                .zip(right)
                .fold(0f32, |peak, (l, r)| max(max(peak, l.abs()), r.abs()))
        });

        self.silent_input_samples = if input_peak < SILENT_PEAK {
            self.silent_input_samples + count as i64
        } else {
            0
        };

        // By now, the analyzers and the dry delay can only hold silence, so nothing here can make a sound.
        let buffers = (self.window_size + latency_for(self.window_size) + count) as i64;
        let is_asleep = self.silent_input_samples >= buffers && self.voices.iter().all(Voice::is_silent);
        if is_asleep {
            for analyzer in &mut self.analyzers {
                analyzer.skip_silence(count);
            }
            let count = count as i32;
            self.control_countdown =
                (self.control_countdown + CONTROL_INTERVAL - count % CONTROL_INTERVAL) % CONTROL_INTERVAL;

            // Nobody can hear parameters move while asleep, and sound shouldn't start with them still ramping.
            self.snap_smoothers();
            return Output::Silence;
        }

        self.render(block.input, block.output);
        Output::Sound
    }

    fn latency(&self) -> u32 {
        latency_for(window_size_for(self.sample_rate)) as u32
    }

    /// The sound stops at the input's silence plus the buffers, and the silence flags say when.
    fn tail(&self) -> Tail {
        let window_size = window_size_for(self.sample_rate);
        Tail::Samples((window_size + latency_for(window_size)) as u32)
    }
}

export_plugin!(PartialResynth);

#[cfg(test)]
mod tests {
    use super::*;
    use hadronyche_plugin::header;
    use std::path::Path;

    #[test]
    fn param_ids_header_is_current() {
        let path = Path::new(env!("CARGO_MANIFEST_DIR")).join("../src/ParamIds.h");
        if let Err(problem) = header::check(&path, &header::param_ids("PartialResynth", &Param::NAMES)) {
            panic!("{problem}");
        }
    }

    #[test]
    fn the_window_follows_the_sample_rate_and_the_latency_the_window() {
        assert_eq!(window_size_for(44100.0), 2048);
        assert_eq!(window_size_for(96000.0), 4096);
        assert_eq!(window_size_for(192000.0), 8192);
        assert_eq!(latency_for(2048), 1536);
    }

    #[test]
    fn the_defaults_are_what_projects_have_always_started_from() {
        let plain: Vec<f64> = PARAMS.iter().map(|spec| spec.default).collect();
        assert_eq!(
            plain,
            [
                -60.0, 0.0, 0.0, 100.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 20000.0, 0.0, 0.0, 0.0, 0.0, 100.0, 0.0
            ]
        );
    }
}
