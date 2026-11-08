//! Pure Data's original fiddle~, as "fiddle~ <windowSize> 1 100 100" with "amp-range <powerFloor> <powerFloor + 10>",
//! behind the C interface in tests/reference/ReferenceFiddle.h.

use core::ffi::{c_float, c_int, c_void};
use std::sync::{Mutex, MutexGuard};

unsafe extern "C" {
    fn referenceFiddleCreate(window_size: c_int, sample_rate: c_float, power_floor: c_float) -> *mut c_void;
    fn referenceFiddleDestroy(fiddle: *mut c_void);
    fn referenceFiddleProcess(fiddle: *mut c_void, samples: *mut c_float, count: c_int) -> c_int;
    fn referenceFiddleGetPeak(fiddle: *mut c_void, index: c_int, frequency: *mut c_float, amplitude: *mut c_float);
    fn referenceFiddleGetTotalPower(fiddle: *mut c_void) -> c_float;
}

/// The original keeps state in statics, its FFT's buffers among them, so one instance at a time: cargo runs tests on
/// parallel threads, where the C++ test program ran one process per test.
static ONE_AT_A_TIME: Mutex<()> = Mutex::new(());

/// One instance of the original, which holds the others off until it's dropped.
pub struct ReferenceFiddle {
    fiddle: *mut c_void,
    _turn: MutexGuard<'static, ()>,
}

impl ReferenceFiddle {
    pub fn new(window_size: usize, sample_rate: f64, power_floor: f32) -> Self {
        // A test that failed while it had the original is no reason for the next one to.
        let turn = ONE_AT_A_TIME.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
        // SAFETY: the arguments are plain values, and the original allocates its own state.
        let fiddle = unsafe { referenceFiddleCreate(window_size as c_int, sample_rate as c_float, power_floor) };
        assert!(!fiddle.is_null(), "the original fiddle~ couldn't be created");
        Self { fiddle, _turn: turn }
    }

    /// Takes a block of samples that fits the hop, and says whether it completed a frame.
    pub fn process(&mut self, samples: &[f32]) -> bool {
        let mut block = samples.to_vec();
        // SAFETY: the block is this call's own copy, of the length given.
        unsafe { referenceFiddleProcess(self.fiddle, block.as_mut_ptr(), block.len() as c_int) != 0 }
    }

    /// The peak at a place in the original's list of 100, as frequency and amplitude. Places beyond the last peak
    /// found have an amplitude of zero.
    pub fn peak(&self, index: usize) -> (f32, f32) {
        let mut frequency = 0f32;
        let mut amplitude = 0f32;
        // SAFETY: the two outputs are this call's own.
        unsafe { referenceFiddleGetPeak(self.fiddle, index as c_int, &mut frequency, &mut amplitude) };
        (frequency, amplitude)
    }

    pub fn total_power(&self) -> f32 {
        // SAFETY: a live instance.
        unsafe { referenceFiddleGetTotalPower(self.fiddle) }
    }
}

impl Drop for ReferenceFiddle {
    fn drop(&mut self) {
        // SAFETY: created by new, and dropped once.
        unsafe { referenceFiddleDestroy(self.fiddle) };
    }
}
