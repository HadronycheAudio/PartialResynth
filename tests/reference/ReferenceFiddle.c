/* Builds Pure Data's fiddle~.c, which isn't changed in any way, against the stub of Pd's API in m_pd.h. */

#define PD
#include "fiddle~.c"

#include "ReferenceFiddle.h"

/* Written separately from the plug-in's FFT, in double precision, so that it can't share its mistakes. */
void pd_fft (t_float* buffer, int npoints, int inverse)
{
    static double re[MAXPOINTS];
    static double im[MAXPOINTS];
    int i, j, k, size;
    const double pi = 3.14159265358979323846;
    (void) inverse;

    for (i = 0, j = 0; i < npoints; i++)
    {
        re[j] = buffer[2 * i];
        im[j] = buffer[2 * i + 1];
        for (k = npoints >> 1; k && (j & k); k >>= 1)
            j ^= k;
        j |= k;
    }

    for (size = 2; size <= npoints; size <<= 1)
        for (i = 0; i < npoints; i += size)
            for (k = 0; k < size / 2; k++)
            {
                const double angle = -2.0 * pi * k / size;
                const double wr = cos (angle);
                const double wi = sin (angle);
                const int a = i + k;
                const int b = a + size / 2;
                const double tr = re[b] * wr - im[b] * wi;
                const double ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }

    for (i = 0; i < npoints; i++)
    {
        buffer[2 * i] = (t_float) re[i];
        buffer[2 * i + 1] = (t_float) im[i];
    }
}

void* referenceFiddleCreate (int windowSize, float sampleRate, float powerFloor)
{
    t_sigfiddle* x;
    t_signal signal;
    t_signal* signals[1];

    fiddle_tilde_setup ();
    x = (t_sigfiddle*) sigfiddle_new ((t_floatarg) windowSize, 1, 100, 100);

    signal.s_vec = NULL;
    signal.s_n = 64;
    signal.s_sr = sampleRate;
    signals[0] = &signal;
    sigfiddle_dsp (x, signals);
    sigfiddle_amprange (x, powerFloor, powerFloor + 10);
    return x;
}

void referenceFiddleDestroy (void* fiddle)
{
    t_sigfiddle* x = (t_sigfiddle*) fiddle;
    sigfiddle_ff (x);
    free (x);
}

int referenceFiddleProcess (void* fiddle, float* samples, int count)
{
    t_sigfiddle* x = (t_sigfiddle*) fiddle;
    t_int w[4];
    const int before = x->x_histphase;

    w[0] = 0;
    w[1] = (t_int) samples;
    w[2] = (t_int) x;
    w[3] = count;
    fiddle_perform (w);
    return x->x_histphase != before;
}

void referenceFiddleGetPeak (void* fiddle, int index, float* frequency, float* amplitude)
{
    t_sigfiddle* x = (t_sigfiddle*) fiddle;
    *frequency = x->x_peakbuf[index].po_freq;
    *amplitude = x->x_peakbuf[index].po_amp;
}

float referenceFiddleGetTotalPower (void* fiddle)
{
    t_sigfiddle* x = (t_sigfiddle*) fiddle;
    return x->x_dbs[x->x_histphase];
}
