/*
Tests the timing of subnormal numbers on x86/x86-64 CPUs.
To run it, use:
  gcc -O2 fp_subnormal_test_x86.c -o fp_subnormal_test_x86
  ./fp_subnormal_test_x86
*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <xmmintrin.h>
#include <pmmintrin.h>

#define ITERATIONS 1000000000ULL

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
    
    // Read start TSC with serialization
    asm volatile("lfence\n\trdtsc" : "=a"(start_tsc) : : "rdx");

    struct timespec req = {0, 50000000}; // 50 ms calibration window
    nanosleep(&req, NULL);

    asm volatile("lfence\n\trdtsc" : "=a"(end_tsc) : : "rdx");
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    double elapsed_sec = (end_time.tv_sec - start_time.tv_sec) +
                         (end_time.tv_nsec - start_time.tv_nsec) * 1e-9;
    return (uint64_t)((end_tsc - start_tsc) / elapsed_sec);
}

// Read MXCSR Register (SSE Control/Status)
static inline uint32_t get_mxcsr(void) {
    return _mm_getcsr();
}

// Set MXCSR Register
static inline void set_mxcsr(uint32_t mxcsr) {
    _mm_setcsr(mxcsr);
}

// Execute dependent chain of FP operations in x86-64 assembly
uint64_t measure_fp_chain(float base, float val, uint64_t iters) {
    uint64_t start = 0, end = 0;
    uint32_t base_bits = float_to_bits(base);
    uint32_t val_bits = float_to_bits(val);

    asm volatile(
        // Move raw 32-bit values into low element of xmm0 and xmm1
        "movd %[v_val], %%xmm0\n\t"
        "movd %[v_base], %%xmm1\n\t"

        // Pipeline barrier & read TSC (Time Stamp Counter)
        "lfence\n\t"
        "rdtsc\n\t"
        "shl $32, %%rdx\n\t"
        "or %%rdx, %%rax\n\t"
        "mov %%rax, %[v_start]\n\t"

        // Dependent FP loop
        "1:\n\t"
        "addss %%xmm0, %%xmm1\n\t"  // Dependent scalar single-precision add
        "subss %%xmm0, %%xmm1\n\t"  // Restores scale to prevent overflow
        "subq $1, %[v_iters]\n\t"
        "jnz 1b\n\t"

        // Pipeline barrier & read end TSC
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

int main(void) {
    printf("Calibrating TSC Frequency...\n");
    uint64_t freq = get_tsc_frequency();
    printf("TSC Frequency: %.2f MHz\n", freq / 1e6);

    // IEEE 754 Single-Precision Floats
    float normal_base = 1.0f;
    float normal_val  = 0.5f;

    // Smallest positive subnormal float: 0x00000001 (~1.4e-45f)
    uint32_t subnormal_bits = 0x00000001;
    float subnormal_val;
    memcpy(&subnormal_val, &subnormal_bits, sizeof(float));
    float subnormal_base = subnormal_val * 2.0f;

    // Read original MXCSR state
    uint32_t mxcsr = get_mxcsr();

    // Clear Flush-to-Zero (FTZ, Bit 15) and Denormals-are-Zero (DAZ, Bit 6)
    // to force hardware subnormal handling according to IEEE 754 standards
    set_mxcsr(mxcsr & ~(_MM_FLUSH_ZERO_MASK | _MM_DENORMALS_ZERO_MASK));

    printf("\n--- Test 1: Flush-to-Zero (FTZ/DAZ) DISABLED (IEEE 754 Compliance) ---\n");

    uint64_t ticks_normal = measure_fp_chain(normal_base, normal_val, ITERATIONS);
    double ns_normal = ((double)ticks_normal / freq) * 1e9;
    printf("Normal FP Loop    : %llu ticks (~%.2f ns/op)\n", 
           (unsigned long long)ticks_normal, ns_normal / (ITERATIONS * 2));

    uint64_t ticks_subnormal = measure_fp_chain(subnormal_base, subnormal_val, ITERATIONS);
    double ns_subnormal = ((double)ticks_subnormal / freq) * 1e9;
    printf("Subnormal FP Loop : %llu ticks (~%.2f ns/op)\n", 
           (unsigned long long)ticks_subnormal, ns_subnormal / (ITERATIONS * 2));

    // Enable Flush-to-Zero (FTZ bit 15) and Denormals-are-Zero (DAZ bit 6)
    set_mxcsr(mxcsr | _MM_FLUSH_ZERO_MASK | _MM_DENORMALS_ZERO_MASK);

    printf("\n--- Test 2: Flush-to-Zero (FTZ/DAZ) ENABLED ---\n");

    ticks_normal = measure_fp_chain(normal_base, normal_val, ITERATIONS);
    ns_normal = ((double)ticks_normal / freq) * 1e9;
    printf("Normal FP Loop    : %llu ticks (~%.2f ns/op)\n", 
           (unsigned long long)ticks_normal, ns_normal / (ITERATIONS * 2));

    ticks_subnormal = measure_fp_chain(subnormal_base, subnormal_val, ITERATIONS);
    ns_subnormal = ((double)ticks_subnormal / freq) * 1e9;
    printf("Subnormal FP Loop : %llu ticks (~%.2f ns/op)\n", 
           (unsigned long long)ticks_subnormal, ns_subnormal / (ITERATIONS * 2));

    // Restore MXCSR
    set_mxcsr(mxcsr);
    return 0;
}