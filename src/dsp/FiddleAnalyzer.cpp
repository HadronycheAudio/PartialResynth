// A port of the peak analysis in fiddle~.c from Pure Data. The original's notice follows, and its full
// licence is in licenses/pure-data.txt.
//
// Copyright (c) 1997-1999 Miller Puckette and Ted Apel.
// For information on usage and redistribution, and for a DISCLAIMER OF ALL WARRANTIES, see the file,
// "LICENSE.txt," in this distribution.

#include "FiddleAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace PartialResynth {

namespace {

constexpr int kMinBin = 3;
constexpr float kDbFudge = 30.8f;
constexpr float kLogToDb = 4.34294481903f; // 20 / log (10)
constexpr float kKnockThreshold = 10.0f;

// A filter kernel for upsampling by two, from the original.
constexpr float kFilter1 = static_cast<float> (.5 * 1.227054);
constexpr float kFilter2 = static_cast<float> (.5 * -0.302385);
constexpr float kFilter3 = static_cast<float> (.5 * 0.095326);
constexpr float kFilter4 = static_cast<float> (.5 * -0.022748);
constexpr float kFilter5 = static_cast<float> (.5 * 0.002533);
constexpr int kFilterSize = 5;

// The original's value of pi for its quarter-wave spiral, which is kept so that the results stay the same.
constexpr double kOriginalPi = 3.14159;
constexpr double kPi = 3.14159265358979323846;

} // namespace

void FiddleAnalyzer::prepare (int windowSize, double sampleRate, int startOffset)
{
    _hop = windowSize / 2;
    _phase = startOffset % _hop;
    _sampleRate = sampleRate;
    _peakCount = 0;
    _totalPower = 0.0f;

    const auto n = static_cast<size_t> (windowSize);
    const auto hop = static_cast<size_t> (_hop);

    _input.assign (hop, 0.0f);
    _lastAnalysis.assign (n + 4 * kFilterSize, 0.0f);
    _spectrum2.assign (n + 4 * kFilterSize, 0.0f);

    // The original's frequency estimate reads a little past the last point it fills in, from whatever is on
    // its stack. Here that's always zero, and it only matters for a peak right at the top of the spectrum.
    _spectrum1.assign (4 * n + 16, 0.0f);

    _spiral.resize (n);
    for (size_t i = 0; i < hop; ++i)
    {
        const double angle = (kOriginalPi * static_cast<double> (i)) / windowSize;
        _spiral[2 * i] = static_cast<float> (std::cos (angle));
        _spiral[2 * i + 1] = static_cast<float> (-std::sin (angle));
    }

    _twiddles.resize (hop);
    for (size_t i = 0; i < hop / 2; ++i)
    {
        const double angle = 2.0 * kPi * static_cast<double> (i) / _hop;
        _twiddles[2 * i] = static_cast<float> (std::cos (angle));
        _twiddles[2 * i + 1] = static_cast<float> (-std::sin (angle));
    }

    _bitReversed.resize (hop);
    int reversed = 0;
    for (int i = 0; i < _hop; ++i)
    {
        _bitReversed[static_cast<size_t> (i)] = reversed;
        int bit = _hop >> 1;
        while (bit != 0 && (reversed & bit) != 0)
        {
            reversed ^= bit;
            bit >>= 1;
        }
        reversed |= bit;
    }
}

float FiddleAnalyzer::getFullScalePower (int windowSize)
{
    // A Hann-windowed sine of amplitude A peaks at A N / 2, and its main lobe's points, half a bin apart, add
    // up to 3 times that peak's power, so total power is 0.75 A^2 N^2.
    return (100.0f - kDbFudge) + kLogToDb * std::log (0.75f * static_cast<float> (windowSize));
}

// A forward, in-place, radix-2 complex FFT of _hop interleaved points. Scaled like the original's: not at all.
void FiddleAnalyzer::fft (float* data) const
{
    for (int i = 0; i < _hop; ++i)
    {
        const int j = _bitReversed[static_cast<size_t> (i)];
        if (j > i)
        {
            std::swap (data[2 * i], data[2 * j]);
            std::swap (data[2 * i + 1], data[2 * j + 1]);
        }
    }

    for (int size = 2; size <= _hop; size *= 2)
    {
        const int half = size / 2;
        const int step = _hop / size;
        for (int start = 0; start < _hop; start += size)
        {
            for (int k = 0; k < half; ++k)
            {
                const float wr = _twiddles[static_cast<size_t> (2 * k * step)];
                const float wi = _twiddles[static_cast<size_t> (2 * k * step + 1)];
                float* a = data + 2 * (start + k);
                float* b = a + 2 * half;
                const float re = b[0] * wr - b[1] * wi;
                const float im = b[0] * wi + b[1] * wr;
                b[0] = a[0] - re;
                b[1] = a[1] - im;
                a[0] += re;
                a[1] += im;
            }
        }
    }
}

// sigfiddle_doit, up to preparing the peaks for output. The variable names are the original's, so that the
// two can be read side by side.
void FiddleAnalyzer::analyze ()
{
    const int hop = _hop;
    const int n = 2 * hop;
    float* spect1 = _spectrum1.data ();
    float* spect2 = _spectrum2.data ();
    float* fp;
    float* fp1;
    float* fp2;
    float* fp3;
    int i;

    const float hzperbin = static_cast<float> (_sampleRate) / (2.0f * n);

    _peakCount = 0;

    // Multiply the H points by a 1/4-wave complex exponential, and take the FFT of the result.
    for (i = 0, fp1 = _input.data (), fp2 = _spiral.data (), fp3 = spect1; i < hop; i++, fp1++, fp2 += 2, fp3 += 2)
    {
        fp3[0] = fp1[0] * fp2[0];
        fp3[1] = fp1[0] * fp2[1];
    }

    fft (spect1);

    // Redistribute the points to get, in effect, the odd-numbered points of the FFT of the H points, zero
    // padded to 4 H in length.
    for (i = 0, fp1 = spect1, fp2 = spect2 + (2 * kFilterSize); i < (hop >> 1); i++, fp1 += 2, fp2 += 4)
    {
        fp2[0] = fp1[0];
        fp2[1] = fp1[1];
    }
    for (i = 0, fp1 = spect1 + n - 2, fp2 = spect2 + (2 * kFilterSize + 2); i < (hop >> 1); i++, fp1 -= 2, fp2 += 4)
    {
        fp2[0] = fp1[0];
        fp2[1] = -fp1[1];
    }
    for (i = 0, fp1 = spect2 + (2 * kFilterSize), fp2 = spect2 + (2 * kFilterSize - 2); i < kFilterSize; i++, fp1 += 2, fp2 -= 2)
    {
        fp2[0] = fp1[0];
        fp2[1] = -fp1[1];
    }
    for (i = 0, fp1 = spect2 + (2 * kFilterSize + n - 2), fp2 = spect2 + (2 * kFilterSize + n); i < kFilterSize; i++, fp1 -= 2, fp2 += 2)
    {
        fp2[0] = fp1[0];
        fp2[1] = -fp1[1];
    }

    // Combine spect2 and the last analysis into spect1. Odd-numbered points of spect1 are the points of the
    // last one plus (-i, i, -i, ...) times the new one. Even-numbered points are the interpolated points of
    // the last one plus (1, -1, 1, ...) times the interpolated points of the new one.
    for (i = 0, fp1 = spect1, fp2 = _lastAnalysis.data () + 2 * kFilterSize, fp3 = spect2 + 2 * kFilterSize; i < (hop >> 1); i++)
    {
        float re = kFilter1 * (fp2[-2] - fp2[1] + fp3[-2] - fp3[1])
                   + kFilter2 * (fp2[-3] - fp2[2] + fp3[-3] - fp3[2])
                   + kFilter3 * (-fp2[-6] + fp2[5] - fp3[-6] + fp3[5])
                   + kFilter4 * (-fp2[-7] + fp2[6] - fp3[-7] + fp3[6])
                   + kFilter5 * (fp2[-10] - fp2[9] + fp3[-10] - fp3[9]);

        float im = kFilter1 * (fp2[-1] + fp2[0] + fp3[-1] + fp3[0])
                   + kFilter2 * (-fp2[-4] - fp2[3] - fp3[-4] - fp3[3])
                   + kFilter3 * (-fp2[-5] - fp2[4] - fp3[-5] - fp3[4])
                   + kFilter4 * (fp2[-8] + fp2[7] + fp3[-8] + fp3[7])
                   + kFilter5 * (fp2[-9] + fp2[8] + fp3[-9] + fp3[8]);

        fp1[0] = 0.7071f * (re + im);
        fp1[1] = 0.7071f * (im - re);
        fp1[4] = fp2[0] + fp3[1];
        fp1[5] = fp2[1] - fp3[0];

        fp1 += 8;
        fp2 += 2;
        fp3 += 2;

        re = kFilter1 * (fp2[-2] - fp2[1] - fp3[-2] + fp3[1])
             + kFilter2 * (fp2[-3] - fp2[2] - fp3[-3] + fp3[2])
             + kFilter3 * (-fp2[-6] + fp2[5] + fp3[-6] - fp3[5])
             + kFilter4 * (-fp2[-7] + fp2[6] + fp3[-7] - fp3[6])
             + kFilter5 * (fp2[-10] - fp2[9] - fp3[-10] + fp3[9]);

        im = kFilter1 * (fp2[-1] + fp2[0] - fp3[-1] - fp3[0])
             + kFilter2 * (-fp2[-4] - fp2[3] + fp3[-4] + fp3[3])
             + kFilter3 * (-fp2[-5] - fp2[4] + fp3[-5] + fp3[4])
             + kFilter4 * (fp2[-8] + fp2[7] - fp3[-8] - fp3[7])
             + kFilter5 * (fp2[-9] + fp2[8] - fp3[-9] - fp3[8]);

        fp1[0] = 0.7071f * (re + im);
        fp1[1] = 0.7071f * (im - re);
        fp1[4] = fp2[0] - fp3[1];
        fp1[5] = fp2[1] + fp3[0];

        fp1 += 8;
        fp2 += 2;
        fp3 += 2;
    }

    std::copy (_spectrum2.begin (), _spectrum2.end (), _lastAnalysis.begin ());

    for (i = 0; i < kMinBin; i++)
        spect1[4 * i + 2] = spect1[4 * i + 3] = 0;

    // Starting at bin kMinBin, compute the Hann-windowed power spectrum.
    float total_power = 0;
    for (i = kMinBin, fp1 = spect1 + 4 * kMinBin; i < n - 2; i++, fp1 += 4)
    {
        const auto re = static_cast<float> (fp1[0] - 0.5 * (fp1[-8] + fp1[8]));
        const auto im = static_cast<float> (fp1[1] - 0.5 * (fp1[-7] + fp1[9]));
        fp1[3] = (total_power += (fp1[2] = re * re + im * im));
    }

    float total_db = 0;
    if (total_power > 1e-9f)
    {
        total_db = (100.f - kDbFudge) + kLogToDb * std::log (total_power / n);
        if (total_db < 0)
            total_db = 0;
    }
    _totalPower = total_db;

    // The second condition isn't in the original, whose floor could be set to zero, and with no power at all,
    // the frequency estimate below would divide zero by zero.
    if (total_db < _powerFloor || total_power <= 1e-9f)
        return;

    // Search for peaks.
    int npeak = 0;
    for (i = kMinBin, fp = spect1 + 4 * kMinBin; i < n - 2 && npeak < kMaxPeaks; i++, fp += 4)
    {
        const float height = fp[2];
        const float h1 = fp[-2];
        const float h2 = fp[6];

        if (height < h1 || height < h2 || h1 < 0.00001f * total_power || h2 < 0.00001f * total_power)
            continue;

        // Use an informal phase vocoder to estimate the frequency, for the two adjacent bins too.
        const float pfreq = ((fp[-8] - fp[8]) * (2.0f * fp[0] - fp[8] - fp[-8])
                             + (fp[-7] - fp[9]) * (2.0f * fp[1] - fp[9] - fp[-7]))
                            / (2.0f * height);
        const float f1 = ((fp[-12] - fp[4]) * (2.0f * fp[-4] - fp[4] - fp[-12])
                          + (fp[-11] - fp[5]) * (2.0f * fp[-3] - fp[5] - fp[-11]))
                             / (2.0f * h1)
                         - 1;
        const float f2 = ((fp[-4] - fp[12]) * (2.0f * fp[4] - fp[12] - fp[-4])
                          + (fp[-3] - fp[13]) * (2.0f * fp[5] - fp[13] - fp[-3]))
                             / (2.0f * h2)
                         + 1;

        // The sample mean and variance of the three.
        const float m = 0.333333f * (pfreq + f1 + f2);
        const float var = 0.5f * ((pfreq - m) * (pfreq - m) + (f1 - m) * (f1 - m) + (f2 - m) * (f2 - m));

        float totalfreq = i + m;
        if (var * total_power > kKnockThreshold * height || var < 1e-30)
            continue;

        if (totalfreq < 4)
            totalfreq = 4;

        const float loudness = std::sqrt (std::sqrt (height));
        _peaks[static_cast<size_t> (npeak)].frequency = hzperbin * totalfreq;
        _peaks[static_cast<size_t> (npeak)].amplitude = static_cast<float> ((2. / static_cast<float> (n)) * (loudness * loudness));
        npeak++;
    }

    _peakCount = npeak;
}

} // namespace PartialResynth
