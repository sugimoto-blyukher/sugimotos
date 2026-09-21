#pragma once

#include "common.h"

struct process;

void arch_init_switch_context(struct process *proc, uint32_t entry);
void arch_init_user_context(struct process *proc, uint32_t entry, uint32_t stack_top,
                            uint32_t argc, uint32_t argv);
uint32_t arch_user_stack_pointer(const struct process *proc);
void arch_clone_user_context(struct process *child, const struct process *parent,
                             uint32_t child_sp);
void arch_resume_from_trap(void);
