/*
Native emulation of the subnormal pixel-stealing SVG filter, for measuring the
data-dependent timing gap on x86 / x86-64 CPUs WITHOUT a browser.

It performs the same arithmetic the browser SVG filter does:
  1. feColorMatrix: multiply every pixel channel by a tiny factor alpha
     (default 1e-40). A white channel (1.0) becomes a SUBNORMAL float; a black
     channel (0.0) stays exactly 0.0.
  2. feConvolveMatrix x repeats: a 1-D convolution with a kernel of `taps` ones
     and divisor = taps (weight w = 1/taps). Every tap is a multiply with a
     subnormal operand -- the slow microcode-assist path on vulnerable Intel
     CPUs -- while a 0.0 input stays 0 * w = 0 on the fast hardware path. The
     divisor keeps the magnitude subnormal across passes.

It times a fully-WHITE buffer against a fully-BLACK buffer. On a vulnerable
Intel CPU white should be dramatically slower; black should match normal speed.
Test 2 re-runs with Flush-to-Zero / Denormals-are-Zero ENABLED: that forces
subnormals to 0 in hardware, so the gap must collapse. A gap that is present
with FTZ off and gone with FTZ on is positive proof the signal is the subnormal
penalty, not cache or memory-layout noise.

Build and run:
  gcc -O2 fp_filter_test_x86.c -o fp_filter_test_x86
  ./fp_filter_test_x86                 # defaults: taps=25 repeats=10 pixels=65536
  ./fp_filter_test_x86 25 10 65536     # taps repeats pixels

Note: no -ffast-math. Default gcc preserves IEEE float semantics, so the
subnormal intermediates (and the penalty) are not optimized away.
*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xmmintrin.h>
#include <pmmintrin.h>

// --- TSC / MXCSR helpers (same approach as fp_subnormal_test_x86.c) ---------

static uint64_t get_tsc_frequency(void) {
    struct timespec start_time, end_time;
    uint64_t start_tsc, end_tsc;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    asm volatile("lfence\n\trdtsc" : "=a"(start_tsc) : : "rdx");
    struct timespec req = {0, 50000000}; // 50 ms calibration window
    nanosleep(&req, NULL);
    asm volatile("lfence\n\trdtsc" : "=a"(end_tsc) : : "rdx");
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed_sec = (end_time.tv_sec - start_time.tv_sec) +
                         (end_time.tv_nsec - start_time.tv_nsec) * 1e-9;
    return (uint64_t)((end_tsc - start_tsc) / elapsed_sec);
}

static inline uint64_t rdtsc_now(void) {
    uint32_t lo, hi;
    asm volatile("lfence\n\trdtsc" : "=a"(lo), "=d"(hi) : : );
    return ((uint64_t)hi << 32) | lo;
}

static inline uint32_t get_mxcsr(void) { return _mm_getcsr(); }
static inline void set_mxcsr(uint32_t mxcsr) { _mm_setcsr(mxcsr); }

// --- The filter -------------------------------------------------------------

// Apply the filter in place over buffers `a` (input) and `b` (scratch), both
// length n. Returns a checksum so the computation cannot be dead-code
// eliminated. `a` is overwritten.
static float run_filter(float *a, float *b, int n, int taps, int repeats, float alpha) {
    // feColorMatrix: scale each channel into (sub)normal range.
    for (int i = 0; i < n; i++) a[i] *= alpha;

    const float w = 1.0f / (float)taps;   // feConvolveMatrix divisor = taps
    const int half = taps / 2;
    for (int r = 0; r < repeats; r++) {
        for (int i = 0; i < n; i++) {
            float acc = 0.0f;
            for (int k = -half; k <= half; k++) {
                int j = i + k;
                if (j < 0) j = 0;          // edge clamp (uniform buffer: no effect)
                if (j >= n) j = n - 1;
                acc += a[j] * w;           // <-- the subnormal MULTIPLY (slow on Intel)
            }
            b[i] = acc;
        }
        float *t = a; a = b; b = t;        // ping-pong
    }

    float s = 0.0f;
    for (int i = 0; i < n; i++) s += a[i];
    return s;
}

// Median of the per-trial tick counts (odd count assumed).
static int cmp_u64(const void *x, const void *y) {
    uint64_t a = *(const uint64_t *)x, b = *(const uint64_t *)y;
    return (a > b) - (a < b);
}

// Time the filter on a buffer uniformly filled with `fill` (runtime value, so
// the compiler cannot constant-fold the pixels). Returns median ticks; writes
// the checksum through `*sink` to keep the work live.
static uint64_t time_filter(float fill, int n, int taps, int repeats, float alpha,
                            volatile float *sink, float *checksum_out) {
    float *a = malloc((size_t)n * sizeof(float));
    float *b = malloc((size_t)n * sizeof(float));
    if (!a || !b) { fprintf(stderr, "out of memory\n"); exit(1); }

    const int TRIALS = 11;
    uint64_t ticks[TRIALS];
    float s = 0.0f;
    for (int t = 0; t < TRIALS; t++) {
        for (int i = 0; i < n; i++) a[i] = fill;
        uint64_t t0 = rdtsc_now();
        s = run_filter(a, b, n, taps, repeats, alpha);
        uint64_t t1 = rdtsc_now();
        ticks[t] = t1 - t0;
        *sink += s;
    }
    free(a); free(b);
    if (checksum_out) *checksum_out = s;   // per-filter output sum of a white/black buffer
    qsort(ticks, TRIALS, sizeof ticks[0], cmp_u64);
    return ticks[TRIALS / 2];
}

static void run_pair(const char *label, int n, int taps, int repeats, float alpha,
                     uint64_t freq, volatile float *sink) {
    float black_sum = 0.0f, white_sum = 0.0f;
    uint64_t black = time_filter(0.0f, n, taps, repeats, alpha, sink, &black_sum); // 0*k = 0
    uint64_t white = time_filter(1.0f, n, taps, repeats, alpha, sink, &white_sum); // -> subnormal
    double mults = (double)n * taps * repeats;
    double ns_black = ((double)black / freq) * 1e9;
    double ns_white = ((double)white / freq) * 1e9;
    printf("%s\n", label);
    printf("  Black buffer : %10llu ticks (~%.2f ns/filter, %.3f ns/mul)\n",
           (unsigned long long)black, ns_black, ns_black / mults);
    printf("  White buffer : %10llu ticks (~%.2f ns/filter, %.3f ns/mul)\n",
           (unsigned long long)white, ns_white, ns_white / mults);
    printf("  White/Black  : %.2fx   (delta %.2f ns/filter)\n",
           (double)white / (double)black, ns_white - ns_black);
    printf("  Output sum   : black=%.3e  white=%.3e  (white!=0 => subnormals survived)\n",
           (double)black_sum, (double)white_sum);
}

int main(int argc, char *argv[]) {
    int taps = 25, repeats = 50, pixels = 8192;   // compute-bound: buffer fits in L1/L2
    if (argc > 1) taps = atoi(argv[1]);
    if (argc > 2) repeats = atoi(argv[2]);
    if (argc > 3) pixels = atoi(argv[3]);
    if (taps < 1 || repeats < 1 || pixels < 1) {
        fprintf(stderr, "Usage: %s [taps] [repeats] [pixels]\n", argv[0]);
        return 1;
    }

    float alpha;
    uint32_t alpha_bits = 0x000016c2; // ~8e-42, a subnormal single
    memcpy(&alpha, &alpha_bits, sizeof(float));

    printf("Calibrating TSC Frequency...\n");
    uint64_t freq = get_tsc_frequency();
    printf("TSC Frequency: %.2f MHz\n", freq / 1e6);
    printf("Filter: taps=%d repeats=%d pixels=%d alpha=%.3e (%d multiplies/filter)\n\n",
           taps, repeats, pixels, (double)alpha, pixels * taps * repeats);

    volatile float sink = 0.0f;
    uint32_t orig_mxcsr = get_mxcsr();

    // Test 1: IEEE compliant -- subnormals are produced and trapped.
    set_mxcsr(orig_mxcsr & ~(_MM_FLUSH_ZERO_MASK | _MM_DENORMALS_ZERO_MASK));
    run_pair("--- Test 1: FTZ/DAZ DISABLED (IEEE 754) -- white should be slow ---",
             pixels, taps, repeats, alpha, freq, &sink);

    printf("\n");

    // Test 2: control -- subnormals flushed to zero in hardware, gap must vanish.
    set_mxcsr(orig_mxcsr | _MM_FLUSH_ZERO_MASK | _MM_DENORMALS_ZERO_MASK);
    run_pair("--- Test 2: FTZ/DAZ ENABLED (control) -- gap should disappear ---",
             pixels, taps, repeats, alpha, freq, &sink);

    set_mxcsr(orig_mxcsr);
    if (sink == 1234.5f) printf("");   // keep `sink` observably live
    return 0;
}
