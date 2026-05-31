#pragma once

#include "common.h"
#include "arch/csr.h"
#include "kernel/proc.h"

#define PANIC(fmt, ...) \
    do { \
        uint32_t __panic_scause = (uint32_t) READ_CSR(scause); \
        uint32_t __panic_stval = (uint32_t) READ_CSR(stval); \
        uint32_t __panic_sepc = (uint32_t) READ_CSR(sepc); \
        printf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
        printf("PANIC_CTX: scause=%x stval=%x sepc=%x satp=%x sstatus=%x\n", \
               __panic_scause, \
               __panic_stval, \
               __panic_sepc, \
               (uint32_t) READ_CSR(satp), \
               (uint32_t) READ_CSR(sstatus)); \
        if (current_proc) { \
            printf("PANIC_PROC: pid=%d state=%d is_user=%d sp=%x proc_sepc=%x proc_satp=%x\n", \
                   current_proc->pid, \
                   current_proc->state, \
                   current_proc->is_user ? 1 : 0, \
                   (uint32_t) current_proc->sp, \
                   current_proc->sepc, \
                   current_proc->satp); \
        } else { \
            printf("PANIC_PROC: none\n"); \
        } \
        for (;;) \
            __asm__ __volatile__("wfi"); \
    } while (0)
