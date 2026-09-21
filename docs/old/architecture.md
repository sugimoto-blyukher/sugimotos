# Monolithic Kernel Directory Layout

This project is organized by kernel subsystem and architecture-specific code.

- `include/`
  - `common.h`: shared types and libc-like interfaces.
  - `kernel/`: kernel-wide structures, macros, and subsystem interfaces.
- `kernel/main.c`: kernel initialization and demo processes.
- `kernel/arch/riscv32/`: RISC-V 32bit low-level entry points (`boot`, trap entry, context switch, linker script).
- `kernel/memory/`: page allocator.
- `kernel/proc/`: process table, fork implementation, scheduler.
- `kernel/syscall/`: syscall dispatcher.
- `kernel/trap/`: trap cause dispatch.
- `kernel/sbi/`: SBI calls and console output.
- `lib/`: freestanding utility routines (`mem*`, `printf`).
- `legacy/`: previous monolithic files kept for reference.
