/*
Tests the timing of subnormal numbers on x86 / x86-64 CPUs.
Supports switching between Addition ('add') and Multiplication ('mul') via CLI.

To compile and run:
  gcc -O2 fp_subnormal_test_x86.c -o fp_subnormal_test_x86
  ./fp_subnormal_test_x86 add
  ./fp_subnormal_test_x86 mul
*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xmmintrin.h>
#include <pmmintrin.h>

#define ITERATIONS 10000000ULL

typedef enum {
    OP_ADD,
    OP_MUL
} OpType;

// Convert float to raw bits safely without UB
static inline uint32_t float_to_bits(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

// Estimate CPU / TSC Frequency in Hz using clock_gettime
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

// Read/Set MXCSR Register (SSE Control/Status)
static inline uint32_t get_mxcsr(void) { return _mm_getcsr(); }
static inline void set_mxcsr(uint32_t mxcsr) { _mm_setcsr(mxcsr); }

// Addition dependent chain (ADDSS / SUBSS)
uint64_t measure_fp_add_chain(float base, float val, uint64_t iters) {
    uint64_t start = 0, end = 0;
    uint32_t base_bits = float_to_bits(base);
    uint32_t val_bits = float_to_bits(val);

    asm volatile(
        "movd %[v_val], %%xmm0\n\t"
        "movd %[v_base], %%xmm1\n\t"

        "lfence\n\t"
        "rdtsc\n\t"
        "shl $32, %%rdx\n\t"
        "or %%rdx, %%rax\n\t"
        "mov %%rax, %[v_start]\n\t"

        "1:\n\t"
        "addss %%xmm0, %%xmm1\n\t"
        "subss %%xmm0, %%xmm1\n\t"
        "subq $1, %[v_iters]\n\t"
        "jnz 1b\n\t"

        "lfence\n\t"
        "rdtsc\n\t"
        "shl $32, %%rdx\n\t"
        "or %%rdx, %%rax\n\t"
        "mov %%rax, %[v_end]\n\t"

        : [v_start] "=&r"(start),
          [v_end] "=&r"(end),
          [v_iters] "+r"(iters)
        : [v_val] "r"(val_bits),
          [v_base] "r"(base_bits)
        : "rax", "rdx", "xmm0", "xmm1", "cc", "memory"
    );

    return end - start;
}

// Multiplication dependent chain (MULSS)
uint64_t measure_fp_mul_chain(float base, float val, uint64_t iters) {
    uint64_t start = 0, end = 0;
    uint32_t base_bits = float_to_bits(base);
    uint32_t val_bits = float_to_bits(val);

    asm volatile(
        "movd %[v_val], %%xmm0\n\t"
        "movd %[v_base], %%xmm1\n\t"

        "lfence\n\t"
        "rdtsc\n\t"
        "shl $32, %%rdx\n\t"
        "or %%rdx, %%rax\n\t"
        "mov %%rax, %[v_start]\n\t"

        "1:\n\t"
        "mulss %%xmm0, %%xmm1\n\t"
        "mulss %%xmm0, %%xmm1\n\t"
        "subq $1, %[v_iters]\n\t"
        "jnz 1b\n\t"

        "lfence\n\t"
        "rdtsc\n\t"
        "shl $32, %%rdx\n\t"
        "or %%rdx, %%rax\n\t"
        "mov %%rax, %[v_end]\n\t"

        : [v_start] "=&r"(start),
          [v_end] "=&r"(end),
          [v_iters] "+r"(iters)
        : [v_val] "r"(val_bits),
          [v_base] "r"(base_bits)
        : "rax", "rdx", "xmm0", "xmm1", "cc", "memory"
    );

    return end - start;
}

int main(int argc, char *argv[]) {
    OpType op = OP_ADD;

    // Parse command line arguments
    if (argc > 1) {
        if (strcmp(argv[1], "mul") == 0 || strcmp(argv[1], "multiply") == 0) {
            op = OP_MUL;
        } else if (strcmp(argv[1], "add") == 0 || strcmp(argv[1], "addition") == 0) {
            op = OP_ADD;
        } else {
            fprintf(stderr, "Usage: %s [add|mul]\n", argv[0]);
            return 1;
        }
    }

    printf("Calibrating TSC Frequency...\n");
    uint64_t freq = get_tsc_frequency();
    printf("TSC Frequency: %.2f MHz\n", freq / 1e6);
    printf("Testing Operation: %s\n", op == OP_ADD ? "ADDITION (ADDSS/SUBSS)" : "MULTIPLICATION (MULSS)");

    float normal_base, normal_val;
    float subnormal_base, subnormal_val;

    if (op == OP_ADD) {
        normal_base = 1.0f;
        normal_val  = 0.5f;

        uint32_t subnormal_bits = 0x00000001;
        memcpy(&subnormal_val, &subnormal_bits, sizeof(float));
        subnormal_base = subnormal_val * 2.0f;
    } else {
        // Multiplication test setup (subnormal * 1.0f remains subnormal)
        normal_base = 1.5f;
        normal_val  = 1.0f;

        uint32_t subnormal_bits = 0x00000100;
        memcpy(&subnormal_base, &subnormal_bits, sizeof(float));
        subnormal_val = 1.0f;
    }

    uint32_t orig_mxcsr = get_mxcsr();
    uint64_t (*measure_func)(float, float, uint64_t) = (op == OP_ADD) ? measure_fp_add_chain : measure_fp_mul_chain;

    // Clear FTZ (Bit 15) & DAZ (Bit 6)
    set_mxcsr(orig_mxcsr & ~(_MM_FLUSH_ZERO_MASK | _MM_DENORMALS_ZERO_MASK));

    printf("\n--- Test 1: Flush-to-Zero (FTZ/DAZ) DISABLED (IEEE 754 Compliance) ---\n");

    uint64_t ticks_normal = measure_func(normal_base, normal_val, ITERATIONS);
    double ns_normal = ((double)ticks_normal / freq) * 1e9;
    printf("Normal FP Loop    : %llu ticks (~%.2f ns/op)\n", 
           (unsigned long long)ticks_normal, ns_normal / (ITERATIONS * 2));

    uint64_t ticks_subnormal = measure_func(subnormal_base, subnormal_val, ITERATIONS);
    double ns_subnormal = ((double)ticks_subnormal / freq) * 1e9;
    printf("Subnormal FP Loop : %llu ticks (~%.2f ns/op)\n", 
           (unsigned long long)ticks_subnormal, ns_subnormal / (ITERATIONS * 2));

    // Enable FTZ & DAZ
    set_mxcsr(orig_mxcsr | _MM_FLUSH_ZERO_MASK | _MM_DENORMALS_ZERO_MASK);

    printf("\n--- Test 2: Flush-to-Zero (FTZ/DAZ) ENABLED ---\n");

    ticks_normal = measure_func(normal_base, normal_val, ITERATIONS);
    ns_normal = ((double)ticks_normal / freq) * 1e9;
    printf("Normal FP Loop    : %llu ticks (~%.2f ns/op)\n", 
           (unsigned long long)ticks_normal, ns_normal / (ITERATIONS * 2));

    ticks_subnormal = measure_func(subnormal_base, subnormal_val, ITERATIONS);
    ns_subnormal = ((double)ticks_subnormal / freq) * 1e9;
    printf("Subnormal FP Loop : %llu ticks (~%.2f ns/op)\n", 
           (unsigned long long)ticks_subnormal, ns_subnormal / (ITERATIONS * 2));

    // Restore original MXCSR
    set_mxcsr(orig_mxcsr);
    return 0;
}