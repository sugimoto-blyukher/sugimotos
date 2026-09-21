#pragma once

#include "common.h"

struct trap_frame {
    uint32_t ra;
    uint32_t gp;
    uint32_t tp;
    uint32_t t0;
    uint32_t t1;
    uint32_t t2;
    uint32_t t3;
    uint32_t t4;
    uint32_t t5;
    uint32_t t6;
    uint32_t a0;
    uint32_t a1;
    uint32_t a2;
    uint32_t a3;
    uint32_t a4;
    uint32_t a5;
    uint32_t a6;
    uint32_t a7;
    uint32_t s0;
    uint32_t s1;
    uint32_t s2;
    uint32_t s3;
    uint32_t s4;
    uint32_t s5;
    uint32_t s6;
    uint32_t s7;
    uint32_t s8;
    uint32_t s9;
    uint32_t s10;
    uint32_t s11;
    uint32_t sp;
    uint32_t sepc;
    uint32_t sstatus;
} __attribute__((aligned(16)));

// Install the trap vector and keep interrupts disabled during boot.
void arch_trap_init(void);
// Enable supervisor external interrupts once the PLIC and scheduler are ready.
void arch_trap_enable_interrupts(void);

void kernel_entry(void);
void handle_trap(struct trap_frame *f);
void arch_handle_ecall(struct trap_frame *f);

extern volatile uint32_t g_last_user_scause;
extern volatile uint32_t g_last_user_stval;
extern volatile uint32_t g_last_user_sepc;
