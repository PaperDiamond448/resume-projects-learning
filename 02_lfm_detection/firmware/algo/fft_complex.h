#ifndef FFT_COMPLEX_H
#define FFT_COMPLEX_H

#include <stddef.h>

/* In-place iterative radix-2 complex FFT/IFFT, single precision.
 * n must be a power of two. inverse=0 -> forward FFT, inverse=1 -> inverse
 * FFT (includes the 1/n scaling, matching MATLAB's ifft()). */
void fft_complex_f32(float *re, float *im, size_t n, int inverse);

#endif
