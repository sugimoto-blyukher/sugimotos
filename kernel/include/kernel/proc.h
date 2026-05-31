#pragma once

#include "common.h"
#include "kernel/trap.h"

#define PROC_MAX 8
#define FD_MAX 16
#define PROC_VMA_MAX 16
#define PROC_UNUSED 0
#define PROC_RUNNABLE 1
#define PROC_BLOCKED 2
#define PROC_ZOMBIE 3

#define VMA_PROT_R 1u
#define VMA_PROT_W 2u
#define VMA_PROT_X 4u

struct file_desc {
    int used;
    int inode;
    uint32_t offset;
    int flags;
};

struct user_vma {
    int used;
    uint32_t start;
    uint32_t end;
    uint32_t prot;
};

struct process {
    int pid;
    int state;
    int priority;
    int budget;
    int parent_pid;
    int exit_status;
    bool is_user;
    vaddr_t sp;
    vaddr_t user_stack_base;
    paddr_t user_stack_paddr;
    uint32_t user_stack_pages;
    uint8_t stack[32768];
    bool has_trap_frame;
    uint32_t sepc;
    uint32_t satp;
    struct trap_frame trap_frame;
    struct user_vma vmas[PROC_VMA_MAX];
    struct file_desc fds[FD_MAX];
};

extern struct process procs[PROC_MAX];
extern struct process *current_proc;
extern struct process *idle_proc;

void switch_context(uint32_t *prev_sp, uint32_t *next_sp);

struct process *create_process(uint32_t pc);
struct process *create_user_process(uint32_t entry_pc);
int proc_fork(struct trap_frame *f, uint32_t user_pc);
int proc_exec(struct trap_frame *f, uint32_t entry_pc, uint32_t argv);
void proc_exit(int status);
void proc_reap_orphan_zombies(void);
int proc_wait(int *status_ptr);
int proc_waitpid(int pid, int *status_ptr, int options);
int proc_user_readable_ok(uint32_t addr, uint32_t len);
int proc_user_writable_ok(uint32_t addr, uint32_t len);
int proc_user_cstr_ok(uint32_t addr, uint32_t max_len);
int proc_user_exec_argv_ok(uint32_t argv_ptr, int *argc_out);
int proc_mmap(uint32_t addr_hint, uint32_t len, uint32_t prot, uint32_t flags);
int proc_munmap(uint32_t addr, uint32_t len);
int proc_handle_user_page_fault(uint32_t fault_addr, uint32_t scause);
void yield(void);
