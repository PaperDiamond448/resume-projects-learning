#include "fft_complex.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static size_t bit_reverse(size_t x, int bits)
{
    size_t r = 0;
    for (int i = 0; i < bits; i++) {
        r = (r << 1) | (x & 1u);
        x >>= 1;
    }
    return r;
}

/* Plain textbook Cooley-Tukey DIT FFT. Not optimized (no precomputed twiddle
 * table, no CMSIS-DSP) -- this is the PC-validated reference used to check
 * correctness of the port. Swap in arm_cfft_f32 on the STM32 side once the
 * algorithm itself is confirmed to be right; see firmware/README.md. */
void fft_complex_f32(float *re, float *im, size_t n, int inverse)
{
    int bits = 0;
    while (((size_t)1 << bits) < n) bits++;

    for (size_t i = 0; i < n; i++) {
        size_t j = bit_reverse(i, bits);
        if (j > i) {
            float t;
            t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }

    float sign = inverse ? 1.0f : -1.0f;
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = (double)sign * 2.0 * M_PI / (double)len;
        float wr = (float)cos(ang), wi = (float)sin(ang);
        for (size_t i = 0; i < n; i += len) {
            float cwr = 1.0f, cwi = 0.0f;
            for (size_t k = 0; k < len / 2; k++) {
                float ur = re[i + k], ui = im[i + k];
                float br = re[i + k + len / 2], bi = im[i + k + len / 2];
                float vr = br * cwr - bi * cwi;
                float vi = br * cwi + bi * cwr;
                re[i + k]           = ur + vr;
                im[i + k]           = ui + vi;
                re[i + k + len / 2] = ur - vr;
                im[i + k + len / 2] = ui - vi;
                float nwr = cwr * wr - cwi * wi;
                float nwi = cwr * wi + cwi * wr;
                cwr = nwr; cwi = nwi;
            }
        }
    }

    if (inverse) {
        for (size_t i = 0; i < n; i++) {
            re[i] /= (float)n;
            im[i] /= (float)n;
        }
    }
}
