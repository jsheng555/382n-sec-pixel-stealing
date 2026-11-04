/*
Tests the timing of subnormal numbers on ARM / Apple Silicon CPUs.
Supports switching between Addition ('add') and Multiplication ('mul') via CLI.

To compile and run on ARM64:
  clang -O2 fp_subnormal_test_arm.c -o fp_subnormal_test_arm
  ./fp_subnormal_test_arm add
  ./fp_subnormal_test_arm mul
*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ITERATIONS 100000000ULL

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

// Execute dependent chain of FP additions (FADD / FSUB) in ARM64 assembly
uint64_t measure_fp_add_chain(float base, float val, uint64_t iters) {
    uint64_t start = 0, end = 0;
    uint32_t base_bits = float_to_bits(base);
    uint32_t val_bits = float_to_bits(val);

    asm volatile(
        "fmov s0, %w[v_val]\n\t"
        "fmov s1, %w[v_base]\n\t"

        "isb\n\t"
        "mrs %[v_start], cntvct_el0\n\t"
        "isb\n\t"

        "1:\n\t"
        "fadd s1, s1, s0\n\t"    // Dependent FP addition
        "fsub s1, s1, s0\n\t"    // Restores scale to prevent float overflow
        "subs %[v_iters], %[v_iters], #1\n\t"
        "b.ne 1b\n\t"

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

// Execute dependent chain of FP multiplications (FMUL) in ARM64 assembly
uint64_t measure_fp_mul_chain(float base, float val, uint64_t iters) {
    uint64_t start = 0, end = 0;
    uint32_t base_bits = float_to_bits(base);
    uint32_t val_bits = float_to_bits(val);

    asm volatile(
        "fmov s0, %w[v_val]\n\t"
        "fmov s1, %w[v_base]\n\t"

        "isb\n\t"
        "mrs %[v_start], cntvct_el0\n\t"
        "isb\n\t"

        "1:\n\t"
        "fmul s1, s1, s0\n\t"    // Dependent FP multiplication
        "fmul s1, s1, s0\n\t"
        "subs %[v_iters], %[v_iters], #1\n\t"
        "b.ne 1b\n\t"

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

    uint64_t freq = get_cntfrq();
    printf("System Timer Frequency: %.2f MHz\n", freq / 1e6);
    printf("Testing Operation: %s\n", op == OP_ADD ? "ADDITION (FADD/FSUB)" : "MULTIPLICATION (FMUL)");

    float normal_base, normal_val;
    float subnormal_base, subnormal_val;

    if (op == OP_ADD) {
        normal_base = 1.0f;
        normal_val  = 0.5f;

        uint32_t subnormal_bits = 0x00000001;
        memcpy(&subnormal_val, &subnormal_bits, sizeof(float));
        subnormal_base = subnormal_val * 2.0f;
    } else {
        // Multiplication setup: subnormal * 1.0f stays subnormal
        normal_base = 1.5f;
        normal_val  = 1.0f;

        uint32_t subnormal_bits = 0x00000100;
        memcpy(&subnormal_base, &subnormal_bits, sizeof(float));
        subnormal_val = 1.0f;
    }

    uint64_t fpcr = get_fpcr();
    uint64_t (*measure_func)(float, float, uint64_t) = (op == OP_ADD) ? measure_fp_add_chain : measure_fp_mul_chain;

    // Clear Flush-to-Zero (FZ, Bit 24 of FPCR) for IEEE 754 compliance
    set_fpcr(fpcr & ~(1ULL << 24));

    printf("\n--- Test 1: Flush-to-Zero (FZ) DISABLED (IEEE 754 Compliance) ---\n");

    uint64_t ticks_normal = measure_func(normal_base, normal_val, ITERATIONS);
    double ns_normal = ((double)ticks_normal / freq) * 1e9;
    printf("Normal FP Loop    : %llu ticks (~%.2f ns/op)\n", ticks_normal, ns_normal / (ITERATIONS * 2));

    uint64_t ticks_subnormal = measure_func(subnormal_base, subnormal_val, ITERATIONS);
    double ns_subnormal = ((double)ticks_subnormal / freq) * 1e9;
    printf("Subnormal FP Loop : %llu ticks (~%.2f ns/op)\n", ticks_subnormal, ns_subnormal / (ITERATIONS * 2));

    // Enable Flush-to-Zero (FZ bit 24 set)
    set_fpcr(fpcr | (1ULL << 24));

    printf("\n--- Test 2: Flush-to-Zero (FZ) ENABLED ---\n");

    ticks_normal = measure_func(normal_base, normal_val, ITERATIONS);
    ns_normal = ((double)ticks_normal / freq) * 1e9;
    printf("Normal FP Loop    : %llu ticks (~%.2f ns/op)\n", ticks_normal, ns_normal / (ITERATIONS * 2));

    ticks_subnormal = measure_func(subnormal_base, subnormal_val, ITERATIONS);
    ns_subnormal = ((double)ticks_subnormal / freq) * 1e9;
    printf("Subnormal FP Loop : %llu ticks (~%.2f ns/op)\n", ticks_subnormal, ns_subnormal / (ITERATIONS * 2));

    // Restore original FPCR
    set_fpcr(fpcr);
    return 0;
}