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

#define PROC_MAX 32
#define FD_MAX 16
#define PROC_VMA_MAX 16
#define PROC_UNUSED 0
#define PROC_RUNNABLE 1
#define PROC_BLOCKED 2
#define PROC_ZOMBIE 3

// User VMA protection bits.
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
    uint8_t stack[8192];
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

paddr_t alloc_pages(uint32_t n);
paddr_t alloc_pages_try(uint32_t n);
void free_pages(paddr_t paddr, uint32_t n);
void page_inc_ref(paddr_t paddr);
void page_dec_ref(paddr_t paddr);
struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4,
                       long arg5, long fid, long eid);
void putchar(char ch);
int getchar(void);
int kbd_enqueue_char(char ch);
void sbi_shutdown(void);

void kernel_entry(void);
void switch_context(uint32_t *prev_sp, uint32_t *next_sp);
void handle_trap(struct trap_frame *f);

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

void fs_init(void);
int fs_open(const char *path, int flags);
int fs_close(int fd);
int fs_read(int fd, void *buf, uint32_t len);
int fs_write(int fd, const void *buf, uint32_t len);
int fs_unlink(const char *path);
int fs_rename(const char *old_path, const char *new_path);
int fs_listdir(char *buf, uint32_t len);

int vm_init(void);
uint32_t vm_kernel_satp(void);
uint32_t vm_build_user_satp(vaddr_t user_stack_base, paddr_t user_stack_paddr, uint32_t user_stack_pages);
void vm_activate(uint32_t satp_value);
int vm_map_user_page(uint32_t satp_value, uint32_t va, uint32_t pa, uint32_t prot);
int vm_unmap_user_page(uint32_t satp_value, uint32_t va);
int vm_query_user_page(uint32_t satp_value, uint32_t va, paddr_t *pa_out, uint32_t *prot_out);

void kernel_main(void);
void boot(void);
