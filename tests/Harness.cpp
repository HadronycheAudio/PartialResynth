#include "Harness.h"

using namespace Steinberg;

namespace PartialResynth {
namespace Tests {

double measureFrequency (const Samples& samples, double around, double sampleRate)
{
    double best = around;
    double bestLevel = -1.0;
    for (double hz = around * 0.9; hz <= around * 1.1; hz += around * 0.0005)
    {
        const double level = toneLevel (samples, hz, 0, sampleRate);
        if (level > bestLevel)
        {
            bestLevel = level;
            best = hz;
        }
    }
    return best;
}

} // namespace Tests
} // namespace PartialResynth
