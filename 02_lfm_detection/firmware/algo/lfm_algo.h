#ifndef LFM_ALGO_H
#define LFM_ALGO_H

/*
 * Port of lfm2_2.m + detpart.m (copy-correlation LFM delay/peak detector)
 * from MATLAB to portable C. This file has no STM32/HAL dependency -- it is
 * driven by firmware/host_test/main_host.c on the PC (for validation against
 * result.txt) and by firmware/stm32/Core/Src/main.c on the target board.
 *
 * Indexing convention: MATLAB is 1-based and the original script leans on
 * quite a few literal index offsets (tig(1023+cnt+1+...), mainwav(count+300),
 * corwingate(count+100+1), ...). To keep the translation mechanically
 * checkable against the .m source, the per-channel state arrays below keep
 * MATLAB's 1-based numbering: array size is N+1 and index 0 is unused.
 */

#include <stdint.h>
#include <stddef.h>

#define LFM_FS          156250.0f
#define LFM_F0          10000.0f
#define LFM_T_PULSE     0.020f
#define LFM_BW          2000.0f
#define LFM_DN          16
#define LFM_NUM_FFT     2048
#define LFM_NUM_SIGR    1024
#define LFM_DATA_LEN    16384
#define LFM_MAX_PEAKS   200
#define LFM_SEL_PEAKS   5
#define LFM_NO_SYNC     65535L   /* MATLAB's "timenum" sentinel for "no sync this block" */

typedef struct {
    float  sta[LFM_MAX_PEAKS + 1];
    float  max[LFM_MAX_PEAKS + 1];
    float  maxl[LFM_MAX_PEAKS + 1];
    float  maxr[LFM_MAX_PEAKS + 1];
    float  wide[LFM_MAX_PEAKS + 1];
    float  Ain[LFM_MAX_PEAKS + 1];
    int    num;
} lfm_peak_list_t;

/* One quantized, reported peak -- mirrors one line of result.txt. */
typedef struct {
    long   block_num;
    int    channel;
    int    peak_id;
    double delay_s;
    double amp_v;
    double wide_ms;
    double in_out_ratio;
} lfm_report_t;

typedef void (*lfm_report_fn)(void *ctx, const lfm_report_t *r);

/* Persistent per-channel state, mirrors the MATLAB script's persistent
 * arrays (tig / mainwav / corwingate / wav1 / temgate / nowtime / peaked). */
typedef struct {
    float  tig_re[LFM_NUM_SIGR + LFM_NUM_SIGR - 1 + 1]; /* index 1..2047 */
    float  tig_im[LFM_NUM_SIGR + LFM_NUM_SIGR - 1 + 1];
    float  mainwav[1324 + 1];      /* index 1..1324 */
    float  corwingate[1124 + 1];   /* index 1..1124 */
    float  wav1[300 + 1];          /* index 1..300 */
    uint16_t pp_copy[142 + 1];     /* index 1..142, raw-sample carry across blocks (raw codes are 0..65535, unsigned -- do NOT use a signed 16-bit type here, see lfm_algo.c header comment) */
    int     pp_copy_valid;         /* 0 on the very first block (mirrors MATLAB's block_num==1 special case) */
    float   temgate;               /* adaptive threshold, carried across blocks */
    long    nowtime;                /* downsampled-domain sample counter since last detpart() trigger */
    lfm_peak_list_t peaked;
} lfm_channel_state_t;

/* Call once at program start (builds BAND_COEF / orth_coef / reference chirp
 * / its FFT). Not per-channel -- all channels share these constant tables. */
void lfm_coeffs_init(void);

void lfm_channel_reset(lfm_channel_state_t *st);

/*
 * Process one DATA_LEN=16384-sample packet for one channel, exactly mirroring
 * one iteration of lfm2_2.m's `for block_num = 1:num_segments` loop body.
 *
 * raw_block : LFM_DATA_LEN raw ADC samples (as-is, e.g. 0..65535 codes -- the
 *             per-window mean is subtracted internally exactly like MATLAB).
 * has_sync  : 1 if this packet contains a known sync/ping position (matches
 *             MATLAB's sync_map.isKey(block_num)); 0 otherwise. Real hardware
 *             normally will not have this a priori -- see lfm_channel_force_select().
 * sync_offset: 1-based raw-sample position (1..16384) of the sync pulse
 *             within this packet -- i.e. MATLAB's `timenum`/`offsets` value,
 *             NOT a 0-based array index. Only meaningful when has_sync=1.
 * channel   : reported verbatim in lfm_report_t.channel / result.txt "Channel".
 *
 * Whenever the internal sync-driven trigger fires, this calls detpart-
 * equivalent peak selection and invokes on_report() once per selected peak
 * (on_report may be NULL to just run the detector without reporting).
 */
void lfm_process_packet(lfm_channel_state_t *st,
                         const uint16_t *raw_block,
                         int has_sync, long sync_offset,
                         long block_num, int channel,
                         lfm_report_fn on_report, void *report_ctx);

/*
 * For hardware use where there is no a-priori sync_map: force a detpart-style
 * peak-selection pass right now (e.g. on a scheduled ping-period timer, or a
 * locally detected sync condition) instead of relying on a known sync sample
 * offset inside lfm_process_packet(). Mirrors the body of the `if
 * (Tsysdis==count+1)` branch in lfm2_2.m without needing a MATLAB-only
 * sync_map.
 */
void lfm_channel_force_select(lfm_channel_state_t *st, long block_num, int channel,
                               lfm_report_fn on_report, void *report_ctx);

#endif
