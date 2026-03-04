#pragma once

#include "common.h"

struct sbiret {
    long error;
    long value;
};

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
} __attribute__((packed));

#define PANIC(fmt, ...) \
    do { \
        printf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
        while (1) {} \
    } while (0)

#define READ_CSR(reg) \
    ({ \
        unsigned long __tmp; \
        __asm__ __volatile__("csrr %0, " #reg : "=r"(__tmp)); \
        __tmp; \
    })

#define WRITE_CSR(reg, value) \
    do { \
        uint32_t __tmp = (value); \
        __asm__ __volatile__("csrw " #reg ", %0" ::"r"(__tmp)); \
    } while (0)

#define PROC_MAX 8
#define FD_MAX 16
#define PROC_UNUSED 0
#define PROC_RUNNABLE 1
#define PROC_BLOCKED 2
#define PROC_ZOMBIE 3

struct file_desc {
    int used;
    int inode;
    uint32_t offset;
    int flags;
};

struct process {
    int pid;
    int state;
    int parent_pid;
    int exit_status;
    bool is_user;
    vaddr_t sp;
    vaddr_t user_stack_base;
    paddr_t user_stack_paddr;
    uint32_t user_stack_pages;
    uint8_t stack[8192];
    bool has_trap_frame;
    uint32_t sepc;
    uint32_t satp;
    struct trap_frame trap_frame;
    struct file_desc fds[FD_MAX];
};

extern struct process procs[PROC_MAX];
extern struct process *current_proc;
extern struct process *idle_proc;

paddr_t alloc_pages(uint32_t n);
void free_pages(paddr_t paddr, uint32_t n);
struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4,
                       long arg5, long fid, long eid);
void putchar(char ch);
int getchar(void);
void sbi_shutdown(void);

void kernel_entry(void);
void switch_context(uint32_t *prev_sp, uint32_t *next_sp);
void handle_trap(struct trap_frame *f);

struct process *create_process(uint32_t pc);
struct process *create_user_process(uint32_t entry_pc);
int proc_fork(struct trap_frame *f, uint32_t user_pc);
int proc_exec(struct trap_frame *f, uint32_t entry_pc, uint32_t argv);
void proc_exit(int status);
int proc_wait(int *status_ptr);
int proc_waitpid(int pid, int *status_ptr, int options);
int proc_user_writable_ok(uint32_t addr, uint32_t len);
int proc_user_cstr_ok(uint32_t addr, uint32_t max_len);
int proc_user_exec_argv_ok(uint32_t argv_ptr, int *argc_out);
void yield(void);

void fs_init(void);
int fs_open(const char *path, int flags);
int fs_close(int fd);
int fs_read(int fd, void *buf, uint32_t len);
int fs_write(int fd, const void *buf, uint32_t len);
int fs_unlink(const char *path);
int fs_rename(const char *old_path, const char *new_path);
int fs_listdir(char *buf, uint32_t len);

void vm_init(void);
uint32_t vm_kernel_satp(void);
uint32_t vm_build_user_satp(vaddr_t user_stack_base, paddr_t user_stack_paddr, uint32_t user_stack_pages);
void vm_activate(uint32_t satp_value);

void kernel_main(void);
void boot(void);
