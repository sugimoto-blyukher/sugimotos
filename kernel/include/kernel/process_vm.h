#pragma once

struct process;

int proc_vm_init(struct process *proc);
void proc_vm_release(struct process *proc);
int proc_vm_clone(struct process *parent, struct process *child);
void proc_vm_reset(struct process *proc);
