/*
 * Line-by-line C translation of lfm2_2.m (main block loop) and detpart.m
 * (peak selection / result quantization). Kept deliberately close to the
 * MATLAB source -- including its 1-based array indexing and a couple of its
 * quirks -- so behaviour can be checked against result.txt sample-for-sample
 * via firmware/host_test/main_host.c before trusting it on real hardware.
 *
 * Known-intentional quirk preserved from the .m source:
 *   BAND_COEF is declared zeros(1,128) but the fill loop writes
 *   BAND_COEF(count+1) for count=1:128, i.e. indices 2..129 -- MATLAB
 *   silently grows the array to 129 elements and index 1 is left at 0.
 *   That leading zero tap is reproduced here (see lfm_coeffs_init).
 */

#include "lfm_algo.h"
#include "fft_complex.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/* Shared constant tables (built once by lfm_coeffs_init, read-only    */
/* afterwards, shared by every channel).                               */
/* ------------------------------------------------------------------ */

#define BAND_TAPS   129   /* see file header comment */
#define ORTH_TAPS   16

static float  g_band_coef[BAND_TAPS + 1];      /* 1-based, index 1..129 */
static float  g_orth_coef1[ORTH_TAPS + 1];     /* 1-based, index 1..16  */
static float  g_orth_coef2[ORTH_TAPS + 1];
static float  g_sc_square;
static float  g_S_re[LFM_NUM_FFT];             /* 0-based, FFT of the padded reference chirp */
static float  g_S_im[LFM_NUM_FFT];
static float  g_agate;                         /* power-gate^2, absolute detection floor */
static const float  INOUTGATE   = 0.3f;
static const float  MULTINUM_K  = 2.0f;
static const int    T0NUM       = 195;         /* floor(T*Fs/Dn) */
static const long   PEAK_OFFSET = 3196;        /* floor(T*Fs+71) */

static void build_reference_chirp(float *s_re, float *s_im /* [0..1023] */)
{
    const float fs = LFM_FS / LFM_DN;                 /* downsampled rate, 9765.625 */
    const int   Nc = (int)lroundf(LFM_T_PULSE * fs);  /* 195 */
    const int   Nl = LFM_NUM_SIGR - Nc;               /* 829 */
    const float k  = LFM_BW / LFM_T_PULSE;            /* chirp rate, 100000 Hz/s */

    memset(s_re, 0, LFM_NUM_SIGR * sizeof(float));
    memset(s_im, 0, LFM_NUM_SIGR * sizeof(float));

    for (int n = 0; n < Nc; n++) {
        double t = (double)n / (double)fs;
        double phase = 2.0 * M_PI * LFM_F0 * t + M_PI * k * t * t;
        s_re[Nl + n] = (float)sin(phase);
        s_im[Nl + n] = (float)cos(phase);
    }
}

void lfm_coeffs_init(void)
{
    /* --- band-pass FIR (with the 129-tap / leading-zero quirk) --- */
    memset(g_band_coef, 0, sizeof(g_band_coef));
    {
        const float Bt = LFM_F0 * 6.0f / 1500.0f;
        const double wl = 2.0 * M_PI * (LFM_F0 - Bt) / LFM_FS;
        const double wh = 2.0 * M_PI * (LFM_F0 + LFM_BW + Bt) / LFM_FS;
        for (int count = 1; count <= 128; count++) {
            double x = (double)count - 129.0 / 2.0; /* (NbandLen+1)/2.0, NbandLen=128 */
            double win = 0.54 - 0.46 * cos(2.0 * M_PI * count / 127.0);
            double val = 32767.0 * win * (sin(x * wh) - sin(x * wl)) / M_PI / x;
            g_band_coef[count + 1] = (float)val;
        }
    }

    /* --- orthogonal (Hilbert-pair) FIR, literal coefficients from the .m --- */
    {
        static const float c1[ORTH_TAPS] = {
            -212,-21,-336,-1298,796,-2963,-3279,17011,17011,-3279,-2963,796,-1298,-336,-21,-212
        };
        static const float c2[ORTH_TAPS] = {
            131,407,30,1519,1912,423,12247,11972,-11972,-12247,-423,-1912,-1519,-30,-407,-131
        };
        for (int i = 1; i <= ORTH_TAPS; i++) {
            g_orth_coef1[i] = c1[i - 1];
            g_orth_coef2[i] = c2[i - 1];
        }
    }

    /* --- reference chirp + its zero-padded 2048-point FFT --- */
    {
        float s_re[LFM_NUM_SIGR], s_im[LFM_NUM_SIGR];
        build_reference_chirp(s_re, s_im);

        g_sc_square = 0.0f;
        for (int i = 0; i < LFM_NUM_SIGR; i++) {
            g_sc_square += s_re[i] * s_re[i];
        }

        memset(g_S_re, 0, sizeof(g_S_re));
        memset(g_S_im, 0, sizeof(g_S_im));
        memcpy(g_S_re, s_re, sizeof(s_re));
        memcpy(g_S_im, s_im, sizeof(s_im));
        fft_complex_f32(g_S_re, g_S_im, LFM_NUM_FFT, 0);
    }

    /* --- absolute detection floor: agate = powergate^2 --- */
    {
        double powergate = 30.0 * 0.001 / 3.3 * 65535.0 * (LFM_T_PULSE * LFM_FS / LFM_DN);
        g_agate = (float)(powergate * powergate);
    }
}

void lfm_channel_reset(lfm_channel_state_t *st)
{
    memset(st, 0, sizeof(*st));
    st->temgate = 100.0f; /* MATLAB: temgate = 100 (initial adaptive-threshold seed) */
}

/* 1-based full linear convolution: a[1..la], b[1..lb] -> out[1..la+lb-1]. */
static void conv1(const float *a, int la, const float *b, int lb, float *out)
{
    int lo = la + lb - 1;
    for (int n = 1; n <= lo; n++) {
        int kmin = (n - lb + 1) > 1 ? (n - lb + 1) : 1;
        int kmax = (n < la) ? n : la;
        float sum = 0.0f;
        for (int k = kmin; k <= kmax; k++) {
            sum += a[k] * b[n - k + 1];
        }
        out[n] = sum;
    }
}

/* trunc-toward-zero, matching MATLAB's fix() */
static float fixf(float x) { return (x >= 0.0f) ? floorf(x) : ceilf(x); }

/* ------------------------------------------------------------------ */
/* detpart.m equivalent: pick <=5 representative peaks out of the      */
/* accumulated candidate list, refine their position by quadratic      */
/* interpolation, quantize, and report them.                           */
/* ------------------------------------------------------------------ */
static void select_and_report(lfm_channel_state_t *st, long block_num, int channel,
                               lfm_report_fn on_report, void *report_ctx)
{
    lfm_peak_list_t *pk = &st->peaked;
    if (pk->num == 0) return;

    float  sel_sta[LFM_SEL_PEAKS + 1], sel_max[LFM_SEL_PEAKS + 1];
    float  sel_wide[LFM_SEL_PEAKS + 1], sel_ain[LFM_SEL_PEAKS + 1];
    int    sel_dix[6 + 1] = {0};
    int    sel_n = 0;
    int    peaknum = pk->num;

    if (peaknum <= 5) {
        for (int i = 1; i <= peaknum; i++) sel_dix[i] = i;
        sel_n = peaknum;
    } else {
        int maxdix[6 + 1] = {0}, fstdix[6 + 1] = {0};
        int nummax = 0, numfst = 0;
        float maxtmp = 0.0f;
        float dly1 = 0.0f, dly2;
        int dix = 0, fsten = 0;

        for (int c = 1; c <= peaknum; c++) {
            if (pk->max[c] > maxtmp) { maxtmp = pk->max[c]; dly1 = pk->sta[c]; dix = c; fsten = 1; }
        }
        if (fsten == 1) {
            fstdix[numfst + 1] = dix;
            for (int c2 = 30; c2 >= 1; c2--) {
                if ((dix - c2 >= 1) &&
                    (dly1 - pk->sta[dix - c2] <= 6250.0f) &&
                    (pk->max[dix - c2] > maxtmp * 0.1f)) {
                    fstdix[numfst + 1] = dix - c2;
                    maxdix[nummax + 1] = dix;
                    nummax++;
                    break;
                }
            }
            numfst++;
        }

        maxtmp = 0.0f;
        dly2 = pk->sta[1];
        dix = 0; fsten = 0;
        if (numfst > 0) {
            for (int c = 1; c <= peaknum; c++) {
                int cond1 = (pk->sta[c] - dly1 > 7031.0f);
                int cond2 = (dly1 - pk->sta[c] > 7031.0f);
                if (pk->max[c] > maxtmp && (cond1 || cond2)) {
                    maxtmp = pk->max[c]; dly2 = pk->sta[c]; dix = c; fsten = 1;
                }
            }
            if (fsten == 1) {
                fstdix[numfst + 1] = dix;
                for (int c2 = 30; c2 >= 1; c2--) {
                    if ((dix - c2 >= 1) &&
                        (dly2 - pk->sta[dix - c2] <= 6250.0f) &&
                        (pk->max[dix - c2] > maxtmp * 0.1f)) {
                        maxdix[nummax + 1] = dix;
                        fstdix[numfst + 1] = dix - c2;
                        nummax++;
                        break;
                    }
                }
                numfst++;
            }
        }

        for (int c = 1; c <= numfst; c++) sel_dix[c] = fstdix[c];
        for (int c = 1; c <= nummax; c++) if (numfst + c <= 5) sel_dix[numfst + c] = maxdix[c];
        sel_n = numfst + nummax;
        if (sel_n > 5) sel_n = 5;

        if (sel_n < 5) {
            /* descending-amplitude fill of whatever indices aren't already selected */
            int order[LFM_MAX_PEAKS + 1];
            for (int i = 1; i <= peaknum; i++) order[i] = i;
            for (int i = 1; i < peaknum; i++) {
                int best = i;
                for (int j = i + 1; j <= peaknum; j++) {
                    if (pk->max[order[j]] > pk->max[order[best]]) best = j;
                }
                int t = order[i]; order[i] = order[best]; order[best] = t;
            }
            int true_sel_n = sel_n;
            for (int j = 1; j <= peaknum && true_sel_n < 5; j++) {
                int flag = 0;
                for (int i = 1; i <= sel_n; i++) if (sel_dix[i] == order[j]) { flag = 1; break; }
                if (!flag) { true_sel_n++; sel_dix[true_sel_n] = order[j]; }
            }
            sel_n = true_sel_n;
        }
    }

    for (int c1 = 1; c1 <= sel_n; c1++) {
        int c2 = sel_dix[c1];
        sel_max[c1]  = pk->max[c2];
        sel_ain[c1]  = pk->Ain[c2];

        float tmpf = 2.0f * pk->max[c2] - pk->maxl[c2] - pk->maxr[c2];
        if (tmpf < 0.00001f && tmpf > 0.0f) tmpf = 0.00001f;
        else if (tmpf > -0.00001f && tmpf <= 0.0f) tmpf = -0.00001f;

        sel_sta[c1] = pk->sta[c2] +
            (float)lroundf((float)LFM_DN * (pk->maxr[c2] - pk->maxl[c2]) / (2.0f * tmpf));
        sel_wide[c1] = (float)lroundf(pk->wide[c2]);
    }

    /* clear the accumulator (mirrors detpart.m zeroing peaked(num) before return) */
    memset(pk, 0, sizeof(*pk));

    for (int idx = 1; idx <= sel_n; idx++) {
        lfm_report_t r;
        r.block_num = block_num;
        r.channel   = channel;
        r.peak_id   = idx;
        r.in_out_ratio = sqrt(sel_max[idx]) / (sel_ain[idx] / INOUTGATE);
        r.amp_v        = sqrt(sel_max[idx]) * 2.0 * 3.3 / 65535.0 / floor(LFM_T_PULSE * LFM_FS / LFM_DN);
        r.wide_ms      = sel_wide[idx] * 1000.0 / LFM_FS;
        r.delay_s      = sel_sta[idx] / LFM_FS;
        if (on_report) on_report(report_ctx, &r);
    }
}

void lfm_channel_force_select(lfm_channel_state_t *st, long block_num, int channel,
                               lfm_report_fn on_report, void *report_ctx)
{
    select_and_report(st, block_num, channel, on_report, report_ctx);
}

void lfm_process_packet(lfm_channel_state_t *st,
                         const uint16_t *raw_block,
                         int has_sync, long sync_offset,
                         long block_num, int channel,
                         lfm_report_fn on_report, void *report_ctx)
{
    /* Large scratch buffers: static (not on the call stack) because this
     * is meant to also run on an STM32 with a modest stack. Processing is
     * inherently single-threaded/sequential, so sharing one scratch set
     * across channels (called one at a time) is safe. */
    static float IN[2191];           /* 1-based 1..2190 */
    static float band_full[2319];    /* 1-based 1..2318 */
    static float orth1_full[2080];   /* 1-based 1..2079 */
    static float orth2_full[2080];
    static float fft_re[LFM_NUM_FFT], fft_im[LFM_NUM_FFT]; /* 0-based */
    static uint16_t PP_1[142 + LFM_DATA_LEN + 1]; /* 1-based 1..(142+16384); MUST be unsigned -- raw codes span 0..65535,
                                                       casting to a *signed* 16-bit type here previously wrapped ~48% of
                                                       samples negative and silently corrupted every downstream stage */

    long timenum = has_sync ? sync_offset : LFM_NO_SYNC;

    /* --- tig history shift: tig(cnt) = tig(cnt+1024), cnt=1..1023 --- */
    for (int cnt = 1; cnt <= 1023; cnt++) {
        st->tig_re[cnt] = st->tig_re[cnt + 1024];
        st->tig_im[cnt] = st->tig_im[cnt + 1024];
    }

    /* --- build PP_1 = [142-sample carry, current 16384-sample packet] --- */
    if (!st->pp_copy_valid) {
        for (int i = 1; i <= 142; i++) PP_1[i] = 0;
    } else {
        for (int i = 1; i <= 142; i++) PP_1[i] = st->pp_copy[i];
    }
    for (int i = 1; i <= LFM_DATA_LEN; i++) PP_1[142 + i] = raw_block[i - 1];

    for (int i = 1; i <= 142; i++) st->pp_copy[i] = raw_block[LFM_DATA_LEN - 142 + i - 1];
    st->pp_copy_valid = 1;

    double meant_acc = 0.0;
    for (int i = 1; i <= 142 + LFM_DATA_LEN; i++) meant_acc += PP_1[i];
    float meant = (float)(meant_acc / (double)(142 + LFM_DATA_LEN));

    /* --- overlap-save filter chain, 8 sub-blocks of 2048 samples --- */
    for (int cnt2 = 1; cnt2 <= 8; cnt2++) {
        for (int cnt = 1; cnt <= 2190; cnt++) {
            IN[cnt] = (float)PP_1[2048 * (cnt2 - 1) + cnt] - meant;
        }

        conv1(g_band_coef, BAND_TAPS, IN, 2190, band_full);
        for (int n = 1; n <= 2318; n++) band_full[n] = fixf(band_full[n] / 32768.0f);

        float *band_sel = band_full + 127; /* band_sel[i] == band_full[i+127], i=1..2064 */

        conv1(g_orth_coef1, ORTH_TAPS, band_sel, 2064, orth1_full);
        conv1(g_orth_coef2, ORTH_TAPS, band_sel, 2064, orth2_full);
        for (int n = 1; n <= 2079; n++) {
            orth1_full[n] = fixf(orth1_full[n] / 32767.0f);
            orth2_full[n] = fixf(orth2_full[n] / 32767.0f);
        }

        float *orth_sel_re = orth1_full + 15; /* orth_sel[i] == orth*_full[i+15], i=1..2048 */
        float *orth_sel_im = orth2_full + 15;

        for (int cnt = 0; cnt <= 127; cnt++) {
            int idx = 1023 + cnt + 1 + (cnt2 - 1) * 128;
            int src = LFM_DN * cnt + 1;
            st->tig_re[idx] = orth_sel_re[src];
            st->tig_im[idx] = orth_sel_im[src];
        }
    }

    /* --- input/output-ratio gate (sliding energy window over tig) --- */
    for (int count = 1; count <= 100; count++) {
        st->corwingate[count] = st->corwingate[1024 + count];
    }
    float sum_gate = 0.0f, gatewin = 0.0f;
    for (int count = 1; count <= 1024; count++) {
        if (count == 1) {
            sum_gate = 0.0f;
            for (int i = 1; i <= T0NUM; i++) {
                float v = st->tig_re[1023 - T0NUM + i];
                sum_gate += v * v;
            }
        } else {
            float v_out = st->tig_re[1023 - T0NUM + count];
            float v_in  = st->tig_re[1023 - T0NUM + count + T0NUM];
            sum_gate = sum_gate - v_out * v_out + v_in * v_in;
        }
        gatewin = sum_gate;
        st->corwingate[count + 100] = sqrtf(gatewin) * sqrtf(g_sc_square) * 2.0f * INOUTGATE;
    }
    st->corwingate[1124] = gatewin; /* faithfully reproduces the .m overwrite of the last slot */

    /* --- frequency-domain copy correlation --- */
    memset(fft_re, 0, sizeof(fft_re));
    memset(fft_im, 0, sizeof(fft_im));
    for (int i = 1; i <= 2047; i++) { fft_re[i - 1] = st->tig_re[i]; fft_im[i - 1] = st->tig_im[i]; }
    fft_complex_f32(fft_re, fft_im, LFM_NUM_FFT, 0); /* Rx */
    for (int i = 0; i < LFM_NUM_FFT; i++) {
        /* corr_freq = Rx .* conj(S) */
        float rr = fft_re[i], ri = fft_im[i];
        float sr = g_S_re[i], si = g_S_im[i];
        fft_re[i] = rr * sr + ri * si;
        fft_im[i] = ri * sr - rr * si;
    }
    fft_complex_f32(fft_re, fft_im, LFM_NUM_FFT, 1); /* freq2 (only first 1024 samples used) */

    /* --- mainwav fill + 300-sample overlap carry --- */
    for (int count = 1; count <= 1024; count++) {
        float re = fft_re[count - 1], im = fft_im[count - 1];
        st->mainwav[300 + count] = re * re + im * im;
    }
    for (int count = 1; count <= 300; count++) {
        st->mainwav[count] = st->wav1[count];
        st->wav1[count] = st->mainwav[count + 1024];
    }

    /* --- double-threshold peak detection + candidate accumulation --- */
    float gate = g_agate;
    float tgate = st->temgate;
    long  Tsysdis = lroundf((float)timenum / (float)LFM_DN);

    for (long count = -1; count <= 1023; count++) {
        if (Tsysdis == count + 1) {
            select_and_report(st, block_num, channel, on_report, report_ctx);
            timenum = LFM_NO_SYNC;
            st->nowtime = 0;
        }

        tgate = tgate * 0.999f + st->mainwav[count + 300] * 0.001f;

        if (st->mainwav[count + 300] > gate && st->mainwav[count + 300] > tgate * MULTINUM_K) {
            if (count != 1023 &&
                st->mainwav[count + 300] > st->mainwav[count + 299] &&
                st->mainwav[count + 300] > st->mainwav[count + 301]) {
                float ingate = st->corwingate[count + 100 + 1];
                if (sqrtf(st->mainwav[count + 300]) > ingate) {
                    float maxtmp = st->mainwav[count + 300] * 0.5f;
                    int cnt = 1;
                    while (cnt < 100 && maxtmp <= st->mainwav[count - cnt + 300]) cnt++;
                    int cnt2v = 1;
                    while (cnt2v < 100 && maxtmp <= st->mainwav[count + cnt2v + 300]) cnt2v++;

                    if (st->peaked.num >= LFM_MAX_PEAKS) {
                        break; /* mirrors the .m `break` out of the whole count loop */
                    }
                    st->peaked.num++;
                    int p = st->peaked.num;
                    st->peaked.sta[p]  = (float)(st->nowtime * LFM_DN - PEAK_OFFSET);
                    st->peaked.max[p]  = st->mainwav[count + 300];
                    st->peaked.maxl[p] = st->mainwav[count + 299];
                    st->peaked.maxr[p] = st->mainwav[count + 301];
                    st->peaked.wide[p] = (float)((cnt + cnt2v) * LFM_DN);
                    st->peaked.Ain[p]  = st->corwingate[count + 100 + 1];
                }
            }
        }

        if (count != -1) st->nowtime++;
    }
    st->temgate = tgate;
}
