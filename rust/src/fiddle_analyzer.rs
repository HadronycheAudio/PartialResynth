//! A port of the peak analysis in fiddle~.c from Pure Data. The original's notice follows, and its full licence is in
//! licenses/pure-data.txt.
//!
//! Copyright (c) 1997-1999 Miller Puckette and Ted Apel.
//! For information on usage and redistribution, and for a DISCLAIMER OF ALL WARRANTIES, see the file, "LICENSE.txt,"
//! in this distribution.
//!
//! The pitch tracking and note detection aren't here, because nothing here uses them.
//!
//! Like the original, it takes half a window of new samples per frame, and works out the spectrum of the whole window
//! from that half's FFT and the previous half's. The arithmetic deliberately stays as close to the original as it
//! can, down to the rounded constants: tests/fiddle.rs runs the original source alongside this and compares every
//! frame, so it isn't "tidied" independently of that. It's also bit for bit the C++ port that came before it, which
//! is why `f32` and `f64` are mixed exactly where the C++ mixed `float` and `double`.
//!
//! It differs from the original in two ways:
//! - It finds peaks up to `MAX_PEAKS` and returns all of them, and the caller chooses. The original outputs the
//!   lowest ones in frequency, and stops looking once it has what it was asked for.
//! - Below the power threshold, the frame has no peaks. The original skips updating its peak list here, so it goes
//!   on reporting the last peaks it found for as long as the input stays quiet.

// The original's rounded constants, 0.7071 and its 3.14159 among them, are kept on purpose: they're what it computes
// with, and the tests hold this to it.
#![allow(clippy::approx_constant, clippy::excessive_precision)]

/// A frame's sinusoidal peak: the same frequency and amplitude as fiddle~'s peak output.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct SpectralPeak {
    /// In Hz.
    pub frequency: f32,
    /// Linear: a full-scale sine reads about 1.
    pub amplitude: f32,
}

pub const MAX_PEAKS: usize = 100;

const MIN_BIN: usize = 3;
const DB_FUDGE: f32 = 30.8;
/// 20 / log (10)
const LOG_TO_DB: f32 = 4.34294481903;
const KNOCK_THRESHOLD: f32 = 10.0;

// A filter kernel for upsampling by two, from the original.
const FILTER1: f32 = (0.5 * 1.227054) as f32;
const FILTER2: f32 = (0.5 * -0.302385) as f32;
const FILTER3: f32 = (0.5 * 0.095326) as f32;
const FILTER4: f32 = (0.5 * -0.022748) as f32;
const FILTER5: f32 = (0.5 * 0.002533) as f32;
const FILTER_SIZE: usize = 5;

/// The original's value of pi for its quarter-wave spiral, which is kept so that the results stay the same.
const ORIGINAL_PI: f64 = 3.14159;
const PI: f64 = core::f64::consts::PI;

#[derive(Clone, Debug)]
pub struct FiddleAnalyzer {
    hop: usize,
    phase: usize,
    sample_rate: f64,
    power_floor: f32,

    input: Vec<f32>,
    last_analysis: Vec<f32>,
    spiral: Vec<f32>,
    spectrum1: Vec<f32>,
    spectrum2: Vec<f32>,
    twiddles: Vec<f32>,
    bit_reversed: Vec<usize>,

    peaks: [SpectralPeak; MAX_PEAKS],
    peak_count: usize,
    total_power: f32,
}

impl Default for FiddleAnalyzer {
    fn default() -> Self {
        Self {
            hop: 0,
            phase: 0,
            sample_rate: 44100.0,
            power_floor: 40.0,
            input: Vec::new(),
            last_analysis: Vec::new(),
            spiral: Vec::new(),
            spectrum1: Vec::new(),
            spectrum2: Vec::new(),
            twiddles: Vec::new(),
            bit_reversed: Vec::new(),
            peaks: [SpectralPeak::default(); MAX_PEAKS],
            peak_count: 0,
            total_power: 0.0,
        }
    }
}

/// An index a signed distance from a base, as the original's pointer arithmetic has it.
fn at(base: usize, offset: isize) -> usize {
    (base as isize + offset) as usize
}

impl FiddleAnalyzer {
    /// Allocates, so this is only for outside the audio thread. `window_size` is fiddle~'s "npoints", a power of two:
    /// a frame arrives every `window_size / 2` samples, the first after that many minus `start_offset`.
    pub fn prepare(&mut self, window_size: usize, sample_rate: f64, start_offset: usize) {
        self.hop = window_size / 2;
        self.phase = start_offset % self.hop;
        self.sample_rate = sample_rate;
        self.peak_count = 0;
        self.total_power = 0.0;

        let n = window_size;
        let hop = self.hop;

        self.input = vec![0.0; hop];
        self.last_analysis = vec![0.0; n + 4 * FILTER_SIZE];
        self.spectrum2 = vec![0.0; n + 4 * FILTER_SIZE];

        // The original's frequency estimate reads a little past the last point it fills in, from whatever is on its
        // stack. Here that's always zero, and it only matters for a peak right at the top of the spectrum.
        self.spectrum1 = vec![0.0; 4 * n + 16];

        self.spiral = vec![0.0; n];
        for i in 0..hop {
            let angle = (ORIGINAL_PI * i as f64) / window_size as f64;
            self.spiral[2 * i] = angle.cos() as f32;
            self.spiral[2 * i + 1] = -angle.sin() as f32;
        }

        self.twiddles = vec![0.0; hop];
        for i in 0..hop / 2 {
            let angle = 2.0 * PI * i as f64 / hop as f64;
            self.twiddles[2 * i] = angle.cos() as f32;
            self.twiddles[2 * i + 1] = -angle.sin() as f32;
        }

        self.bit_reversed = vec![0; hop];
        let mut reversed = 0;
        for i in 0..hop {
            self.bit_reversed[i] = reversed;
            let mut bit = hop >> 1;
            while bit != 0 && (reversed & bit) != 0 {
                reversed ^= bit;
                bit >>= 1;
            }
            reversed |= bit;
        }
    }

    /// In fiddle~'s scale, its "amp-range" low value, which it compares against total power.
    pub fn set_power_floor(&mut self, power_floor: f32) {
        self.power_floor = power_floor;
    }

    /// On fiddle~'s scale, what total power a full-scale sine gives for a window size, about 101 for 2048.
    /// Subtracting this converts that scale to dB below a full-scale sine.
    pub fn full_scale_power(window_size: usize) -> f32 {
        // A Hann-windowed sine of amplitude A peaks at A N / 2, and its main lobe's points, half a bin apart, add up
        // to 3 times that peak's power, so total power is 0.75 A^2 N^2.
        (100.0 - DB_FUDGE) + LOG_TO_DB * (0.75 * window_size as f32).ln()
    }

    /// Returns true when this sample completes a frame, which is then in `peaks`.
    pub fn push(&mut self, sample: f32) -> bool {
        self.input[self.phase] = sample;
        self.phase += 1;
        if self.phase < self.hop {
            return false;
        }

        self.phase = 0;
        self.analyze();
        true
    }

    /// Moves on by `count` samples of silence, for an analyzer that has already taken in at least a window of
    /// silence. Its buffers can only hold zeros at that point, so this is the same as pushing those zeros.
    pub fn skip_silence(&mut self, count: usize) {
        self.phase = (self.phase + count) % self.hop;
    }

    /// In ascending frequency, the order they're found in.
    pub fn peaks(&self) -> &[SpectralPeak] {
        &self.peaks[..self.peak_count]
    }

    /// fiddle~'s scale, as its envelope outlet gives it.
    pub fn total_power(&self) -> f32 {
        self.total_power
    }

    /// A forward, in-place, radix-2 complex FFT of `hop` interleaved points. Scaled like the original's: not at all.
    fn fft(data: &mut [f32], hop: usize, twiddles: &[f32], bit_reversed: &[usize]) {
        for (i, &j) in bit_reversed.iter().enumerate() {
            if j > i {
                data.swap(2 * i, 2 * j);
                data.swap(2 * i + 1, 2 * j + 1);
            }
        }

        let mut size = 2;
        while size <= hop {
            let half = size / 2;
            let step = hop / size;
            for start in (0..hop).step_by(size) {
                for k in 0..half {
                    let wr = twiddles[2 * k * step];
                    let wi = twiddles[2 * k * step + 1];
                    let a = 2 * (start + k);
                    let b = a + 2 * half;
                    let re = data[b] * wr - data[b + 1] * wi;
                    let im = data[b] * wi + data[b + 1] * wr;
                    data[b] = data[a] - re;
                    data[b + 1] = data[a + 1] - im;
                    data[a] += re;
                    data[a + 1] += im;
                }
            }
            size *= 2;
        }
    }

    /// sigfiddle_doit, up to preparing the peaks for output. The variable names are the original's, so that the two
    /// can be read side by side, and `fp1`, `fp2` and `fp3` are its pointers as indices into the named buffers.
    fn analyze(&mut self) {
        let hop = self.hop;
        let n = 2 * hop;
        let spect1 = &mut self.spectrum1;
        let spect2 = &mut self.spectrum2;

        let hzperbin = self.sample_rate as f32 / (2.0 * n as f32);

        self.peak_count = 0;

        // Multiply the H points by a 1/4-wave complex exponential, and take the FFT of the result.
        for i in 0..hop {
            spect1[2 * i] = self.input[i] * self.spiral[2 * i];
            spect1[2 * i + 1] = self.input[i] * self.spiral[2 * i + 1];
        }

        Self::fft(spect1, hop, &self.twiddles, &self.bit_reversed);

        // Redistribute the points to get, in effect, the odd-numbered points of the FFT of the H points, zero padded
        // to 4 H in length.
        {
            let (mut fp1, mut fp2) = (0, 2 * FILTER_SIZE);
            for _ in 0..hop >> 1 {
                spect2[fp2] = spect1[fp1];
                spect2[fp2 + 1] = spect1[fp1 + 1];
                fp1 += 2;
                fp2 += 4;
            }
        }
        {
            let (mut fp1, mut fp2) = (n - 2, 2 * FILTER_SIZE + 2);
            for _ in 0..hop >> 1 {
                spect2[fp2] = spect1[fp1];
                spect2[fp2 + 1] = -spect1[fp1 + 1];
                fp1 -= 2;
                fp2 += 4;
            }
        }
        for i in 0..FILTER_SIZE {
            let fp1 = 2 * FILTER_SIZE + 2 * i;
            let fp2 = 2 * FILTER_SIZE - 2 - 2 * i;
            spect2[fp2] = spect2[fp1];
            spect2[fp2 + 1] = -spect2[fp1 + 1];
        }
        {
            let (mut fp1, mut fp2) = (2 * FILTER_SIZE + n - 2, 2 * FILTER_SIZE + n);
            for _ in 0..FILTER_SIZE {
                spect2[fp2] = spect2[fp1];
                spect2[fp2 + 1] = -spect2[fp1 + 1];
                fp1 -= 2;
                fp2 += 2;
            }
        }

        // Combine spect2 and the last analysis into spect1. Odd-numbered points of spect1 are the points of the last
        // one plus (-i, i, -i, ...) times the new one. Even-numbered points are the interpolated points of the last
        // one plus (1, -1, 1, ...) times the interpolated points of the new one.
        {
            let last = &self.last_analysis;
            let (mut fp1, mut fp2, mut fp3) = (0, 2 * FILTER_SIZE, 2 * FILTER_SIZE);
            for _ in 0..hop >> 1 {
                let l = |offset: isize| last[at(fp2, offset)];
                let s = |offset: isize| spect2[at(fp3, offset)];

                let re = FILTER1 * (l(-2) - l(1) + s(-2) - s(1))
                    + FILTER2 * (l(-3) - l(2) + s(-3) - s(2))
                    + FILTER3 * (-l(-6) + l(5) - s(-6) + s(5))
                    + FILTER4 * (-l(-7) + l(6) - s(-7) + s(6))
                    + FILTER5 * (l(-10) - l(9) + s(-10) - s(9));

                let im = FILTER1 * (l(-1) + l(0) + s(-1) + s(0))
                    + FILTER2 * (-l(-4) - l(3) - s(-4) - s(3))
                    + FILTER3 * (-l(-5) - l(4) - s(-5) - s(4))
                    + FILTER4 * (l(-8) + l(7) + s(-8) + s(7))
                    + FILTER5 * (l(-9) + l(8) + s(-9) + s(8));

                spect1[fp1] = 0.7071 * (re + im);
                spect1[fp1 + 1] = 0.7071 * (im - re);
                spect1[fp1 + 4] = l(0) + s(1);
                spect1[fp1 + 5] = l(1) - s(0);

                fp1 += 8;
                fp2 += 2;
                fp3 += 2;

                let l = |offset: isize| last[at(fp2, offset)];
                let s = |offset: isize| spect2[at(fp3, offset)];

                let re = FILTER1 * (l(-2) - l(1) - s(-2) + s(1))
                    + FILTER2 * (l(-3) - l(2) - s(-3) + s(2))
                    + FILTER3 * (-l(-6) + l(5) + s(-6) - s(5))
                    + FILTER4 * (-l(-7) + l(6) + s(-7) - s(6))
                    + FILTER5 * (l(-10) - l(9) - s(-10) + s(9));

                let im = FILTER1 * (l(-1) + l(0) - s(-1) - s(0))
                    + FILTER2 * (-l(-4) - l(3) + s(-4) + s(3))
                    + FILTER3 * (-l(-5) - l(4) + s(-5) + s(4))
                    + FILTER4 * (l(-8) + l(7) - s(-8) - s(7))
                    + FILTER5 * (l(-9) + l(8) - s(-9) - s(8));

                spect1[fp1] = 0.7071 * (re + im);
                spect1[fp1 + 1] = 0.7071 * (im - re);
                spect1[fp1 + 4] = l(0) - s(1);
                spect1[fp1 + 5] = l(1) + s(0);

                fp1 += 8;
                fp2 += 2;
                fp3 += 2;
            }
        }

        self.last_analysis.copy_from_slice(spect2);

        for i in 0..MIN_BIN {
            spect1[4 * i + 2] = 0.0;
            spect1[4 * i + 3] = 0.0;
        }

        // Starting at bin MIN_BIN, compute the Hann-windowed power spectrum.
        let mut total_power = 0f32;
        for i in MIN_BIN..n - 2 {
            let fp1 = 4 * i;
            let re = (f64::from(spect1[fp1]) - 0.5 * f64::from(spect1[fp1 - 8] + spect1[fp1 + 8])) as f32;
            let im = (f64::from(spect1[fp1 + 1]) - 0.5 * f64::from(spect1[fp1 - 7] + spect1[fp1 + 9])) as f32;
            spect1[fp1 + 2] = re * re + im * im;
            total_power += spect1[fp1 + 2];
            spect1[fp1 + 3] = total_power;
        }

        let mut total_db = 0f32;
        if total_power > 1e-9 {
            total_db = (100.0 - DB_FUDGE) + LOG_TO_DB * (total_power / n as f32).ln();
            if total_db < 0.0 {
                total_db = 0.0;
            }
        }
        self.total_power = total_db;

        // The second condition isn't in the original, whose floor could be set to zero, and with no power at all, the
        // frequency estimate below would divide zero by zero.
        if total_db < self.power_floor || total_power <= 1e-9 {
            return;
        }

        // Search for peaks.
        let mut npeak = 0;
        for i in MIN_BIN..n - 2 {
            if npeak >= MAX_PEAKS {
                break;
            }
            let fp = 4 * i;
            let f = |offset: isize| spect1[at(fp, offset)];

            let height = f(2);
            let h1 = f(-2);
            let h2 = f(6);

            if height < h1 || height < h2 || h1 < 0.00001 * total_power || h2 < 0.00001 * total_power {
                continue;
            }

            // Use an informal phase vocoder to estimate the frequency, for the two adjacent bins too.
            let pfreq = ((f(-8) - f(8)) * (2.0 * f(0) - f(8) - f(-8)) + (f(-7) - f(9)) * (2.0 * f(1) - f(9) - f(-7)))
                / (2.0 * height);
            let f1 = ((f(-12) - f(4)) * (2.0 * f(-4) - f(4) - f(-12))
                + (f(-11) - f(5)) * (2.0 * f(-3) - f(5) - f(-11)))
                / (2.0 * h1)
                - 1.0;
            let f2 = ((f(-4) - f(12)) * (2.0 * f(4) - f(12) - f(-4)) + (f(-3) - f(13)) * (2.0 * f(5) - f(13) - f(-3)))
                / (2.0 * h2)
                + 1.0;

            // The sample mean and variance of the three.
            let m = 0.333333 * (pfreq + f1 + f2);
            let var = 0.5 * ((pfreq - m) * (pfreq - m) + (f1 - m) * (f1 - m) + (f2 - m) * (f2 - m));

            let mut totalfreq = i as f32 + m;
            if var * total_power > KNOCK_THRESHOLD * height || f64::from(var) < 1e-30 {
                continue;
            }

            if totalfreq < 4.0 {
                totalfreq = 4.0;
            }

            let loudness = height.sqrt().sqrt();
            self.peaks[npeak].frequency = hzperbin * totalfreq;
            self.peaks[npeak].amplitude = ((2.0 / f64::from(n as f32)) * f64::from(loudness * loudness)) as f32;
            npeak += 1;
        }

        self.peak_count = npeak;
    }
}
