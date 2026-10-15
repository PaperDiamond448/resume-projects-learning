/*
 * PC-side test harness for the LFM copy-correlation port (firmware/algo).
 * Feeds channel-1 raw ADC data (exported by prepare_input.py from
 * signal_all_35_50.mat) through lfm_process_packet() block by block, exactly
 * like lfm2_2.m's `for block_num = 1:num_segments` loop, and writes
 * result_c.txt in the same layout as the original result.txt so the two can
 * be diffed directly.
 *
 * Usage: main_host <ch1_sig.u16> <ch1_syn.u8> <out_result.txt>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../algo/lfm_algo.h"

static FILE *g_out = NULL;
static int   g_header_written = 0;

static void report_cb(void *ctx, const lfm_report_t *r)
{
    (void)ctx;
    if (!g_header_written) {
        fprintf(g_out, "Block\tChannel\tPeakID\tDelay\tAmp\tWid\tInOutRatio\n");
        g_header_written = 1;
    }
    fprintf(g_out, "%ld\t%d\t%d\t%.4f\t%.4f\t%.1f\t%.2f\n",
            r->block_num, r->channel, r->peak_id,
            r->delay_s, r->amp_v, r->wide_ms, r->in_out_ratio);
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <ch1_sig.u16> <ch1_syn.u8> <out_result.txt>\n", argv[0]);
        return 1;
    }

    FILE *fsig = fopen(argv[1], "rb");
    FILE *fsyn = fopen(argv[2], "rb");
    if (!fsig || !fsyn) { perror("fopen input"); return 1; }

    fseek(fsig, 0, SEEK_END); long sig_bytes = ftell(fsig); fseek(fsig, 0, SEEK_SET);
    fseek(fsyn, 0, SEEK_END); long syn_bytes = ftell(fsyn); fseek(fsyn, 0, SEEK_SET);

    long sig_len = sig_bytes / 2;
    long syn_len = syn_bytes;

    uint16_t *sig = (uint16_t *)malloc((size_t)sig_len * sizeof(uint16_t));
    uint8_t  *syn = (uint8_t  *)malloc((size_t)syn_len);
    if (!sig || !syn) { fprintf(stderr, "out of memory\n"); return 1; }

    if (fread(sig, 2, (size_t)sig_len, fsig) != (size_t)sig_len) { fprintf(stderr, "short read sig\n"); return 1; }
    if (fread(syn, 1, (size_t)syn_len, fsyn) != (size_t)syn_len) { fprintf(stderr, "short read syn\n"); return 1; }
    fclose(fsig); fclose(fsyn);

    long num_segments = (sig_len + LFM_DATA_LEN - 1) / LFM_DATA_LEN;

    /* sync_map: block_num(1-based) -> raw-sample offset within that block (0..16383).
     * "last write wins" for duplicate keys, matching MATLAB's containers.Map(keys,vals)
     * constructor semantics when the same key appears more than once. */
    long  *sync_offset = (long *)calloc((size_t)num_segments + 2, sizeof(long));
    char  *has_sync    = (char *)calloc((size_t)num_segments + 2, sizeof(char));

    for (long i = 1; i <= syn_len; i++) {
        if (syn[i - 1] == 1) {
            long seg = (i + LFM_DATA_LEN - 1) / LFM_DATA_LEN; /* ceil(i/16384), 1-based */
            long off = i - (seg - 1) * LFM_DATA_LEN;          /* 1-based offset within block, 1..16384 -- this IS MATLAB's `timenum` value, do not convert to 0-based */
            if (seg >= 1 && seg <= num_segments) {
                sync_offset[seg] = off;
                has_sync[seg] = 1;
            }
        }
    }

    g_out = fopen(argv[3], "w");
    if (!g_out) { perror("fopen output"); return 1; }

    lfm_coeffs_init();
    lfm_channel_state_t st;
    lfm_channel_reset(&st);

    uint16_t block[LFM_DATA_LEN];
    for (long b = 1; b <= num_segments; b++) {
        long start = (b - 1) * LFM_DATA_LEN;
        long avail = sig_len - start;
        if (avail >= LFM_DATA_LEN) {
            memcpy(block, sig + start, LFM_DATA_LEN * sizeof(uint16_t));
        } else {
            if (avail > 0) memcpy(block, sig + start, (size_t)avail * sizeof(uint16_t));
            for (long i = avail; i < LFM_DATA_LEN; i++) block[i] = 0;
        }

        lfm_process_packet(&st, block, has_sync[b], sync_offset[b], b, /*channel=*/1,
                            report_cb, NULL);

        if (b % 50 == 0) fprintf(stderr, "block %ld / %ld\n", b, num_segments);
    }

    fclose(g_out);
    fprintf(stderr, "done: %ld blocks processed -> %s\n", num_segments, argv[3]);
    return 0;
}
