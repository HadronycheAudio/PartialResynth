# PartialResynth signal path

**Requirement** marks what the owner specified. **Proposed** marks a choice made during implementation where the brief says nothing, or that needed interpreting. Those are open to change.

```
             ┌─→ Dry delay ───────────────────────────────────────────────┐
In (stereo) ─┤                                                            ├→ Dry/wet → Volume → Out
             └─→ Mono → 2 × fiddle~ → Tracker → 5 voices → Spread ────────┘
```

## Analysis

- **Requirement:** the analysis works as `fiddle~` from Pure Data does, using its code as the design. `FiddleAnalyzer` is a port of its peak analysis.
- **Requirement:** a window of 2048 samples. This resolves partials about 86 Hz apart at 44.1 and 48 kHz.
- **Requirement:** a frame every 512 samples. `fiddle~` itself is fixed at half a window, so this runs two copies of it, 512 samples apart. Each frame is calculated just as `fiddle~` would, and this halves the time between frames.
- **Proposed:** the input is summed to mono for the analysis.
- **Proposed:** the window follows the sample rate, to keep its length in time: 2048 up to 64 kHz, 4096 up to 128 kHz, and 8192 above that, along with the hop and the latency.
- **Proposed:** the pitch tracking and note and attack detection in `fiddle~` aren't ported, since only its peaks are used.

### Differences from fiddle~

- **Requirement:** it takes the five loudest peaks. `fiddle~` outputs the first it finds, going up from the lowest frequency.
- **Proposed:** below the threshold, there are no peaks, so the voices fade out. `fiddle~` goes on reporting the last peaks it found for as long as the input stays below its threshold, which would make the voices drone. `fiddle-floor` shows this.

### Threshold

- **Proposed:** it's `fiddle~`'s "amp-range" low value, shown as dB below a full-scale sine, from -90 to -20 dB. The default of -60 dB is about the same as `fiddle~`'s default of 40. It applies to the input's total power, not to each partial.

Measured against the original, across eight signals including harmonic tones, noise, a sweep and onsets out of silence, and three window sizes: the same peaks in every frame, with the worst frequency difference 0.0005 Hz and the worst amplitude difference 0.0000001.

## Tracker

- **Requirement:** a voice follows one partial through time, rather than playing a position in a frame's list of peaks.
- **Proposed:** a peak is the same partial as a voice's if it's within a semitone, per frame. They're matched closest first.
- **Proposed:** a peak being played counts as 3 dB louder when choosing the loudest, so that two peaks of about the same level, with one place between them, don't take turns.
- **Proposed:** a voice with no peak fades out over one frame, keeping its pitch. Only a silent voice takes on a new partial. When all the voices are busy, a new partial waits until a voice has faded, which is one frame.

## Voices

- **Requirement:** five voices, one per partial, each with two oscillators and a filter. All of them share one set of controls.
- **Requirement:** the envelope is always the one from the analysis, and there's no other.
- **Proposed:** the level ramps linearly from one frame's amplitude to the next. The pitch does the same, in octaves.
- **Proposed:** the level is applied after the filter, so the voice has no tail once its partial has gone.

### Oscillators

- **Requirement:** sine, saw or square.
- **Proposed:** each has a level and is tuned relative to the partial, by up to 24 semitones and 100 cents each way. A new instance has oscillator 1 as a sine at full level, and oscillator 2 at zero, which plays the input's partials back. This is a starting point rather than a sound.
- **Proposed:** the saw and square are band-limited with a 4-point polynomial step correction. As in RabidPhatt, there's no oversampling, and they fade out between a fifth and a quarter of the sample rate. A sine plays up to half the sample rate.
- **Proposed:** the oscillators pause while their voice is silent. They all start at phase zero, so the same input always renders the same way.

### Filter

- **Requirement:** a filter per voice.
- **Proposed:** a 12 dB/octave state-variable filter, with low-pass, high-pass, band-pass and notch modes. Resonance takes Q from 0.707 to 20.

### Filter modulation

- **Requirement:** key follow and envelope to cutoff, both shared by all the voices. The analysis stays the only source of envelope.
- **Proposed:** key follow is centred on 440 Hz. At 100%, a partial an octave above 440 Hz has the cutoff an octave above the knob.
- **Proposed:** the envelope is the level in dB, from nothing at -60 dB to the whole amount at 0 dB. Going by linear amplitude, most partials would hardly move the filter. The amount is up to 8 octaves each way.

### Glide

- **Requirement:** a control that smooths the voices' pitch.
- **Proposed:** its time is how long it takes to cover 95% of the interval, up to 2 s. It's in addition to the ramp between frames.
- **Proposed:** a new partial starts on its pitch, and doesn't glide until the window has filled with it. Its first frames see it through a part-filled window, which puts its pitch slightly off, and a long glide would hold on to that.

## Output

- **Requirement:** stereo spread for the voices.
- **Proposed:** at 100%, the voices are at the centre, half left, half right, hard left and hard right, in that order. It's at constant power, and at 0% the left and right channels are identical, at the same level as with no pan law at all. Hard-panned voices are therefore 3 dB up on that side.
- **Proposed:** Dry/wet at 50% plays both at full level, as in LoopTexturer. The dry path stays in stereo, and is delayed to line up with the voices.
- **Proposed:** Volume goes from -60 dB, which is silent, to +6 dB. There's no limiter.

## Timing

- **Requirement:** as little latency as is reasonable, with a window of 2048.
- **Proposed:** the reported latency is half the window, because a frame describes what happened around its window's centre, plus the hop, because the voices take that long to reach it. That's 1536 samples at 44.1 and 48 kHz, or 34.8 and 32 ms.

Measured for a 1 kHz tone at 48 kHz starting out of silence, the voice reaches half level 8 samples before the reported latency, and the dry signal arrives exactly on it.

## Sleep

- **Proposed:** after a silent input for longer than the buffers, with all the voices silent, the plug-in stops working and sets the silence flags. The analyzers keep their place, so the output doesn't depend on the host's block size.

## Levels

Measured with a default instance, on five sines from -10.5 to -32 dB: every partial came out within 0.3 dB. A sine in the left channel alone plays at half its level, in both channels, because the analysis sums them.
