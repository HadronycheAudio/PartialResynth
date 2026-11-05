#pragma once

#include "FiddleAnalyzer.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace PartialResynth {

constexpr int kVoiceCount = 5;

// Chooses which peaks to play and which voice plays each of them, so that a voice follows one partial
// through time rather than the position that partial happens to have in a frame's list of peaks.
class PartialTracker
{
public:
    struct Target
    {
        float frequency { 0.0f };
        float amplitude { 0.0f };
        bool isNew { false }; // This voice was silent, and starts a different partial.
    };

    using Targets = std::array<Target, kVoiceCount>;

    void reset () { _targets = Targets {}; }

    const Targets& update (const SpectralPeak* peaks, int peakCount)
    {
        const int chosenCount = chooseLoudest (peaks, peakCount);

        std::array<bool, kVoiceCount> isVoiceTaken {};
        std::array<bool, kVoiceCount> isPeakTaken {};
        continuePartials (peaks, chosenCount, isVoiceTaken, isPeakTaken);

        // A voice with no partial left fades out, and keeps its pitch while it does.
        std::array<bool, kVoiceCount> isFadingOut {};
        for (size_t v = 0; v < kVoiceCount; ++v)
        {
            _targets[v].isNew = false;
            isFadingOut[v] = !isVoiceTaken[v] && _targets[v].amplitude > 0.0f;
            if (!isVoiceTaken[v])
                _targets[v].amplitude = 0.0f;
        }

        startPartials (peaks, chosenCount, isVoiceTaken, isFadingOut, isPeakTaken);
        return _targets;
    }

private:
    // Beyond this between frames, a peak counts as a different partial. It's about what vibrato and a fast
    // slide cover in a frame. Past that, the old partial fades as the new one comes in.
    static constexpr float kMaxJumpSemitones = 1.0f;

    // A peak that a voice is already playing needs to be beaten by this much (3 dB) to lose its place. Without
    // it, two peaks of about the same level, with one place between them, would take turns in every frame.
    static constexpr float kKeepAdvantage = 1.41f;

    static float semitonesBetween (float a, float b) { return std::abs (17.3123405f * std::log (a / b)); }

    bool isBeingPlayed (float frequency) const
    {
        return std::any_of (_targets.begin (), _targets.end (), [frequency] (const Target& target) {
            return target.amplitude > 0.0f && semitonesBetween (target.frequency, frequency) <= kMaxJumpSemitones;
        });
    }

    int chooseLoudest (const SpectralPeak* peaks, int peakCount)
    {
        for (int p = 0; p < peakCount; ++p)
        {
            _scores[static_cast<size_t> (p)] = peaks[p].amplitude * (isBeingPlayed (peaks[p].frequency) ? kKeepAdvantage : 1.0f);
            _order[static_cast<size_t> (p)] = p;
        }

        const int chosenCount = std::min (peakCount, kVoiceCount);
        std::partial_sort (_order.begin (), _order.begin () + chosenCount, _order.begin () + peakCount, [this] (int a, int b) {
            return _scores[static_cast<size_t> (a)] > _scores[static_cast<size_t> (b)];
        });
        return chosenCount;
    }

    // Repeatedly takes the closest pair of a sounding voice and a chosen peak.
    void continuePartials (const SpectralPeak* peaks, int chosenCount, std::array<bool, kVoiceCount>& isVoiceTaken,
                           std::array<bool, kVoiceCount>& isPeakTaken)
    {
        for (;;)
        {
            float closest = kMaxJumpSemitones;
            int bestVoice = -1;
            int bestChosen = -1;

            for (int v = 0; v < kVoiceCount; ++v)
            {
                const auto& target = _targets[static_cast<size_t> (v)];
                if (isVoiceTaken[static_cast<size_t> (v)] || target.amplitude <= 0.0f)
                    continue;

                for (int c = 0; c < chosenCount; ++c)
                {
                    if (isPeakTaken[static_cast<size_t> (c)])
                        continue;

                    const float distance = semitonesBetween (target.frequency, peaks[_order[static_cast<size_t> (c)]].frequency);
                    if (distance <= closest)
                    {
                        closest = distance;
                        bestVoice = v;
                        bestChosen = c;
                    }
                }
            }

            if (bestVoice < 0)
                return;

            const auto& peak = peaks[_order[static_cast<size_t> (bestChosen)]];
            _targets[static_cast<size_t> (bestVoice)].frequency = peak.frequency;
            _targets[static_cast<size_t> (bestVoice)].amplitude = peak.amplitude;
            isVoiceTaken[static_cast<size_t> (bestVoice)] = true;
            isPeakTaken[static_cast<size_t> (bestChosen)] = true;
        }
    }

    // Only a silent voice takes on a new partial, because changing a pitch that can be heard would be a jump.
    // A voice that's fading out finishes before the next frame, so a peak with nowhere to go now gets its
    // place then.
    void startPartials (const SpectralPeak* peaks, int chosenCount, const std::array<bool, kVoiceCount>& isVoiceTaken,
                        const std::array<bool, kVoiceCount>& isFadingOut, const std::array<bool, kVoiceCount>& isPeakTaken)
    {
        int voice = 0;
        for (int c = 0; c < chosenCount; ++c)
        {
            if (isPeakTaken[static_cast<size_t> (c)])
                continue;

            while (voice < kVoiceCount && (isVoiceTaken[static_cast<size_t> (voice)] || isFadingOut[static_cast<size_t> (voice)]))
                ++voice;
            if (voice == kVoiceCount)
                return;

            const auto& peak = peaks[_order[static_cast<size_t> (c)]];
            auto& target = _targets[static_cast<size_t> (voice)];
            target.frequency = peak.frequency;
            target.amplitude = peak.amplitude;
            target.isNew = true;
            ++voice;
        }
    }

    Targets _targets;
    std::array<float, FiddleAnalyzer::kMaxPeaks> _scores {};
    std::array<int, FiddleAnalyzer::kMaxPeaks> _order {};
};

} // namespace PartialResynth
