/*
Tests the timing of subnormal numbers on Apple silicon.
To run it, use:
  clang -O2 fp_subnormal_test.c -o fp_subnormal_test
  ./fp_subnormal_test
*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define ITERATIONS 1000000000ULL

// Convert float to raw bits safely without UB
static inline uint32_t float_to_bits(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

// Read System Frequency (Hz)
static inline uint64_t get_cntfrq(void) {
    uint64_t freq;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    return freq;
}

// Read Floating-Point Control Register (FPCR)
static inline uint64_t get_fpcr(void) {
    uint64_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    return fpcr;
}

// Set FPCR
static inline void set_fpcr(uint64_t fpcr) {
    asm volatile("msr fpcr, %0" :: "r"(fpcr));
}

// Execute dependent chain of FP operations in ARM64 assembly
uint64_t measure_fp_chain(float base, float val, uint64_t iters) {
    uint64_t start = 0, end = 0;
    uint32_t base_bits = float_to_bits(base);
    uint32_t val_bits = float_to_bits(val);

    asm volatile(
        // Move raw bits from general-purpose registers to 32-bit FP registers (s0, s1)
        "fmov s0, %w[v_val]\n\t"
        "fmov s1, %w[v_base]\n\t"

        // Barrier & read system counter (CNTVCT_EL0)
        "isb\n\t"
        "mrs %[v_start], cntvct_el0\n\t"
        "isb\n\t"

        // Dependent FP loop
        "1:\n\t"
        "fadd s1, s1, s0\n\t"    // Dependent FP addition
        "fsub s1, s1, s0\n\t"    // Restores scale to prevent float overflow
        "subs %[v_iters], %[v_iters], #1\n\t"
        "b.ne 1b\n\t"

        // Barrier & read end time
        "isb\n\t"
        "mrs %[v_end], cntvct_el0\n\t"
        "isb\n\t"

        : [v_start] "=&r"(start),
          [v_end] "=&r"(end),
          [v_iters] "+r"(iters)
        : [v_val] "r"(val_bits),
          [v_base] "r"(base_bits)
        : "s0", "s1", "cc", "memory"
    );

    return end - start;
}

int main(void) {
    uint64_t freq = get_cntfrq();
    printf("System Timer Frequency: %.2f MHz\n\t", freq / 1e6);

    // IEEE 754 Single-Precision Floats
    float normal_base = 1.0f;
    float normal_val  = 0.5f;

    // Smallest positive subnormal float: 0x00000001 (~1.4e-45f)
    uint32_t subnormal_bits = 0x00000001;
    float subnormal_val;
    memcpy(&subnormal_val, &subnormal_bits, sizeof(float));
    float subnormal_base = subnormal_val * 2.0f;

    // Clear Flush-to-Zero (FZ, Bit 24 of FPCR) to force hardware subnormal handling
    uint64_t fpcr = get_fpcr();
    set_fpcr(fpcr & ~(1ULL << 24));

    printf("\n--- Test 1: Flush-to-Zero (FZ) DISABLED (IEEE 754 Compliance) ---\n");
    
    uint64_t ticks_normal = measure_fp_chain(normal_base, normal_val, ITERATIONS);
    double ns_normal = ((double)ticks_normal / freq) * 1e9;
    printf("Normal FP Loop    : %llu ticks (~%.2f ns/op)\n", ticks_normal, ns_normal / (ITERATIONS * 2));

    uint64_t ticks_subnormal = measure_fp_chain(subnormal_base, subnormal_val, ITERATIONS);
    double ns_subnormal = ((double)ticks_subnormal / freq) * 1e9;
    printf("Subnormal FP Loop : %llu ticks (~%.2f ns/op)\n", ticks_subnormal, ns_subnormal / (ITERATIONS * 2));

    // Enable Flush-to-Zero (FZ bit set)
    set_fpcr(fpcr | (1ULL << 24));

    printf("\n--- Test 2: Flush-to-Zero (FZ) ENABLED ---\n");

    ticks_normal = measure_fp_chain(normal_base, normal_val, ITERATIONS);
    ns_normal = ((double)ticks_normal / freq) * 1e9;
    printf("Normal FP Loop    : %llu ticks (~%.2f ns/op)\n", ticks_normal, ns_normal / (ITERATIONS * 2));

    ticks_subnormal = measure_fp_chain(subnormal_base, subnormal_val, ITERATIONS);
    ns_subnormal = ((double)ticks_subnormal / freq) * 1e9;
    printf("Subnormal FP Loop : %llu ticks (~%.2f ns/op)\n", ticks_subnormal, ns_subnormal / (ITERATIONS * 2));

    // Restore FPCR
    set_fpcr(fpcr);
    return 0;
}
