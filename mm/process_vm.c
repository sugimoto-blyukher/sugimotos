#include "kernel/page_alloc.h"
#include "kernel/proc.h"
#include "kernel/process_vm.h"
#include "kernel/vm.h"
#include "uapi/mman.h"

#define USER_MMAP_BASE 0x20000000u
#define USER_MMAP_END 0x3f000000u
#define PTE_W_BIT (1u << 2)

static int proc_add_vma(struct process *proc, uint32_t start, uint32_t end, uint32_t prot)
{
    if (!proc || start >= end)
        return -1;
    if (!is_aligned(start, PAGE_SIZE) || !is_aligned(end, PAGE_SIZE))
        return -1;

    for (int i = 0; i < PROC_VMA_MAX; i++) {
        if (!proc->vmas[i].used)
            continue;
        uint32_t vs = proc->vmas[i].start;
        uint32_t ve = proc->vmas[i].end;
        if (!(end <= vs || start >= ve))
            return -1;
    }

    for (int i = 0; i < PROC_VMA_MAX; i++) {
        if (proc->vmas[i].used)
            continue;
        proc->vmas[i].used = 1;
        proc->vmas[i].start = start;
        proc->vmas[i].end = end;
        proc->vmas[i].prot = prot;
        return 0;
    }
    return -1;
}

static int proc_find_vma_index(const struct process *proc, uint32_t addr)
{
    for (int i = 0; i < PROC_VMA_MAX; i++) {
        if (!proc->vmas[i].used)
            continue;
        if (addr >= proc->vmas[i].start && addr < proc->vmas[i].end)
            return i;
    }
    return -1;
}

static int proc_vma_range_ok(const struct process *proc, uint32_t addr, uint32_t len, uint32_t prot_need)
{
    if (!proc || !proc->is_user)
        return 0;
    if (len == 0)
        return 0;

    uint32_t end = addr + len;
    if (end < addr)
        return 0;

    uint32_t cur = addr;
    while (cur < end) {
        int idx = proc_find_vma_index(proc, cur);
        if (idx < 0)
            return 0;
        const struct user_vma *v = &proc->vmas[idx];
        if ((v->prot & prot_need) != prot_need)
            return 0;
        if (end <= v->end)
            return 1;
        cur = v->end;
    }
    return 1;
}

void proc_vm_release(struct process *proc)
{
    if (!proc || !proc->is_user)
        return;

    for (int i = 0; i < PROC_VMA_MAX; i++) {
        if (!proc->vmas[i].used)
            continue;
        uint32_t vs = proc->vmas[i].start;
        uint32_t ve = proc->vmas[i].end;
        if (vs < USER_MMAP_BASE || ve > USER_MMAP_END)
            continue;
        for (uint32_t va = vs; va < ve; va += PAGE_SIZE) {
            paddr_t pa = 0;
            if (vm_query_user_page(proc->satp, va, &pa, NULL) <= 0)
                continue;
            if (vm_unmap_user_page(proc->satp, va) == 0)
                free_pages(pa, 1);
        }
    }

    // COW can replace each stack page independently; the original allocation
    // is no longer the owner of the live stack after a fork.
    if (!proc->satp) {
        if (proc->user_stack_paddr && proc->user_stack_pages > 0)
            free_pages(proc->user_stack_paddr, proc->user_stack_pages);
        return;
    }
    for (uint32_t i = 0; i < proc->user_stack_pages; i++) {
        uint32_t va = proc->user_stack_base + i * PAGE_SIZE;
        paddr_t pa = 0;
        if (vm_query_user_page(proc->satp, va, &pa, NULL) > 0 &&
            vm_unmap_user_page(proc->satp, va) == 0)
            free_pages(pa, 1);
    }
}

extern char __user_text_start[], __user_text_end[];
extern char __user_rodata_start[], __user_rodata_end[];
extern char __user_data_start[], __user_data_end[];
extern char __user_bss_start[], __user_bss_end[];

static int user_readable_ok(uint32_t addr, uint32_t len)
{
    return proc_vma_range_ok(current_proc, addr, len, VMA_PROT_R);
}

static int user_writable_ok(uint32_t addr, uint32_t len)
{
    return proc_vma_range_ok(current_proc, addr, len, VMA_PROT_W);
}

int proc_user_writable_ok(uint32_t addr, uint32_t len)
{
    if (!current_proc || !current_proc->is_user)
        return 1;
    return user_writable_ok(addr, len);
}

int proc_user_readable_ok(uint32_t addr, uint32_t len)
{
    if (!current_proc || !current_proc->is_user)
        return 1;
    return user_readable_ok(addr, len);
}

int proc_user_cstr_ok(uint32_t addr, uint32_t max_len)
{
    if (!current_proc || !current_proc->is_user)
        return 1;
    if (max_len == 0)
        return 0;
    if (!user_readable_ok(addr, 1))
        return 0;
    for (uint32_t i = 0; i < max_len; i++) {
        if (!user_readable_ok(addr + i, 1))
            return 0;
        if (*(const char *) (addr + i) == '\0')
            return 1;
    }
    return 0;
}

int proc_user_exec_argv_ok(uint32_t argv_ptr, int *argc_out)
{
    if (argc_out)
        *argc_out = 0;

    if (!current_proc || !current_proc->is_user)
        return -1;

    if (argv_ptr == 0) {
        if (argc_out)
            *argc_out = 0;
        return 0;
    }

    const int max_args = 32;
    for (int i = 0; i < max_args; i++) {
        uint32_t slot = argv_ptr + (uint32_t) i * sizeof(uint32_t);
        if (!user_readable_ok(slot, sizeof(uint32_t)))
            return -1;
        uint32_t argp = *(const uint32_t *) slot;
        if (argp == 0) {
            if (argc_out)
                *argc_out = i;
            return 0;
        }
        if (!proc_user_cstr_ok(argp, 128))
            return -1;
    }
    return -1;
}

static uint32_t page_floor(uint32_t v)
{
    return v & ~(PAGE_SIZE - 1u);
}

static uint32_t page_ceil(uint32_t v)
{
    return (v + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
}

int proc_vm_init(struct process *proc)
{
    uint32_t ut0 = page_floor((uint32_t) __user_text_start);
    uint32_t ut1 = page_ceil((uint32_t) __user_text_end);
    uint32_t ur0 = page_floor((uint32_t) __user_rodata_start);
    uint32_t ur1 = page_ceil((uint32_t) __user_rodata_end);
    uint32_t ud0 = page_floor((uint32_t) __user_data_start);
    uint32_t ud1 = page_ceil((uint32_t) __user_data_end);
    uint32_t ub0 = page_floor((uint32_t) __user_bss_start);
    uint32_t ub1 = page_ceil((uint32_t) __user_bss_end);
    uint32_t st0 = proc->user_stack_base;
    uint32_t st1 = proc->user_stack_base + proc->user_stack_pages * PAGE_SIZE;

    memset(proc->vmas, 0, sizeof(proc->vmas));
    if (ut1 > ut0 && proc_add_vma(proc, ut0, ut1, VMA_PROT_R | VMA_PROT_X) < 0)
        return -1;
    if (ur1 > ur0 && proc_add_vma(proc, ur0, ur1, VMA_PROT_R) < 0)
        return -1;
    if (ud1 > ud0 && proc_add_vma(proc, ud0, ud1, VMA_PROT_R | VMA_PROT_W) < 0)
        return -1;
    if (ub1 > ub0 && proc_add_vma(proc, ub0, ub1, VMA_PROT_R | VMA_PROT_W) < 0)
        return -1;
    if (st1 > st0 && proc_add_vma(proc, st0, st1, VMA_PROT_R | VMA_PROT_W) < 0)
        return -1;
    return 0;
}

int proc_vm_clone(struct process *parent, struct process *child)
{
    for (int i = 0; i < PROC_VMA_MAX; i++) {
        if (!parent->vmas[i].used || !(parent->vmas[i].prot & VMA_PROT_W))
            continue;

        uint32_t cow_prot = parent->vmas[i].prot & ~VMA_PROT_W;
        for (uint32_t va = parent->vmas[i].start; va < parent->vmas[i].end; va += PAGE_SIZE) {
            paddr_t parent_pa = 0;
            if (vm_query_user_page(parent->satp, va, &parent_pa, NULL) <= 0)
                continue;

            paddr_t old_child_pa = 0;
            int child_had_mapping = (vm_query_user_page(child->satp, va, &old_child_pa, NULL) > 0);
            if (vm_map_user_page(parent->satp, va, parent_pa, cow_prot) < 0)
                return -1;

            page_inc_ref(parent_pa);
            if (vm_map_user_page(child->satp, va, parent_pa, cow_prot) < 0) {
                page_dec_ref(parent_pa);
                return -1;
            }

            if (child_had_mapping && old_child_pa != parent_pa)
                free_pages(old_child_pa, 1);
        }
    }

    return 0;
}

void proc_vm_reset(struct process *proc)
{
    for (int i = 0; i < PROC_VMA_MAX; i++) {
        if (!proc->vmas[i].used)
            continue;
        if (proc->vmas[i].start >= USER_MMAP_BASE && proc->vmas[i].end <= USER_MMAP_END) {
            for (uint32_t va = proc->vmas[i].start; va < proc->vmas[i].end; va += PAGE_SIZE) {
                paddr_t pa = 0;
                if (vm_query_user_page(proc->satp, va, &pa, NULL) <= 0)
                    continue;
                if (vm_unmap_user_page(proc->satp, va) == 0)
                    free_pages(pa, 1);
            }
            memset(&proc->vmas[i], 0, sizeof(proc->vmas[i]));
        }
    }
}

static int prot_to_query_bits(uint32_t prot)
{
    uint32_t bits = 0;
    if (prot & VMA_PROT_R)
        bits |= (1u << 1);
    if (prot & VMA_PROT_W)
        bits |= (1u << 2);
    if (prot & VMA_PROT_X)
        bits |= (1u << 3);
    return (int) bits;
}

static int proc_find_free_mmap_range(struct process *proc, uint32_t len, uint32_t *addr_out)
{
    uint32_t addr = USER_MMAP_BASE;
    while (addr + len >= addr && addr + len <= USER_MMAP_END) {
        bool overlap = false;
        for (int i = 0; i < PROC_VMA_MAX; i++) {
            if (!proc->vmas[i].used)
                continue;
            uint32_t vs = proc->vmas[i].start;
            uint32_t ve = proc->vmas[i].end;
            if (addr < ve && addr + len > vs) {
                addr = page_ceil(ve);
                overlap = true;
                break;
            }
        }
        if (!overlap) {
            *addr_out = addr;
            return 0;
        }
    }
    return -1;
}

static int proc_range_overlaps_any_vma(const struct process *proc, uint32_t start, uint32_t end)
{
    for (int i = 0; i < PROC_VMA_MAX; i++) {
        if (!proc->vmas[i].used)
            continue;
        uint32_t vs = proc->vmas[i].start;
        uint32_t ve = proc->vmas[i].end;
        if (start < ve && end > vs)
            return 1;
    }
    return 0;
}

static int proc_find_free_vma_slot(struct process *proc)
{
    for (int i = 0; i < PROC_VMA_MAX; i++) {
        if (!proc->vmas[i].used)
            return i;
    }
    return -1;
}

static int proc_try_cow_fault(uint32_t va, const struct user_vma *v)
{
    if (!v || !(v->prot & VMA_PROT_W))
        return -1;

    paddr_t old_pa = 0;
    uint32_t mapped = 0;
    if (vm_query_user_page(current_proc->satp, va, &old_pa, &mapped) <= 0)
        return -1;
    if (mapped & PTE_W_BIT)
        return -1;

    paddr_t new_pa = alloc_pages_try(1);
    if (!new_pa)
        return -1;
    memcpy((void *) new_pa, (const void *) old_pa, PAGE_SIZE);
    if (vm_map_user_page(current_proc->satp, va, new_pa, v->prot) < 0) {
        free_pages(new_pa, 1);
        return -1;
    }
    page_dec_ref(old_pa);
    return 0;
}

int proc_mmap(uint32_t addr_hint, uint32_t len, uint32_t prot, uint32_t flags)
{
    if (!current_proc || !current_proc->is_user)
        return -1;
    if (len == 0)
        return -1;
    if ((prot & ~(VMA_PROT_R | VMA_PROT_W | VMA_PROT_X)) != 0 || prot == 0)
        return -1;

    len = page_ceil(len);
    uint32_t addr = 0;

    if (flags & MAP_FIXED) {
        if (!is_aligned(addr_hint, PAGE_SIZE))
            return -1;
        addr = addr_hint;
        if (addr < USER_MMAP_BASE || addr + len > USER_MMAP_END || addr + len < addr)
            return -1;
        if (proc_munmap(addr, len) < 0)
            return -1;
    } else {
        if (addr_hint != 0 && is_aligned(addr_hint, PAGE_SIZE)) {
            uint32_t hint_end = addr_hint + len;
            if (addr_hint >= USER_MMAP_BASE && hint_end <= USER_MMAP_END && hint_end >= addr_hint &&
                !proc_range_overlaps_any_vma(current_proc, addr_hint, hint_end)) {
                addr = addr_hint;
            }
        }
        if (addr == 0 && proc_find_free_mmap_range(current_proc, len, &addr) < 0)
            return -1;
    }

    if (proc_add_vma(current_proc, addr, addr + len, prot) < 0)
        return -1;
    return (int) addr;
}

int proc_munmap(uint32_t addr, uint32_t len)
{
    if (!current_proc || !current_proc->is_user)
        return -1;
    if (!is_aligned(addr, PAGE_SIZE))
        return -1;
    if (len == 0)
        return -1;
    len = page_ceil(len);

    uint32_t end = addr + len;
    if (end < addr)
        return -1;

    if (addr < USER_MMAP_BASE || end > USER_MMAP_END)
        return -1;

    int changed = 0;
    for (int i = 0; i < PROC_VMA_MAX; i++) {
        if (!current_proc->vmas[i].used)
            continue;

        uint32_t vs = current_proc->vmas[i].start;
        uint32_t ve = current_proc->vmas[i].end;
        if (ve <= addr || vs >= end)
            continue;

        uint32_t cut_s = (addr > vs) ? addr : vs;
        uint32_t cut_e = (end < ve) ? end : ve;
        if (cut_s >= cut_e)
            continue;

        changed = 1;
        if (cut_s == vs && cut_e == ve) {
            memset(&current_proc->vmas[i], 0, sizeof(current_proc->vmas[i]));
            continue;
        }
        if (cut_s == vs) {
            current_proc->vmas[i].start = cut_e;
            continue;
        }
        if (cut_e == ve) {
            current_proc->vmas[i].end = cut_s;
            continue;
        }

        int free_idx = proc_find_free_vma_slot(current_proc);
        if (free_idx < 0)
            return -1;
        current_proc->vmas[free_idx].used = 1;
        current_proc->vmas[free_idx].start = cut_e;
        current_proc->vmas[free_idx].end = ve;
        current_proc->vmas[free_idx].prot = current_proc->vmas[i].prot;
        current_proc->vmas[i].end = cut_s;
    }

    if (!changed)
        return 0;

    for (uint32_t va = addr; va < end; va += PAGE_SIZE) {
        paddr_t pa = 0;
        if (vm_query_user_page(current_proc->satp, va, &pa, NULL) <= 0)
            continue;
        if (vm_unmap_user_page(current_proc->satp, va) == 0)
            free_pages(pa, 1);
    }

    return 0;
}

int proc_handle_user_page_fault(uint32_t fault_addr, uint32_t access)
{
    if (!current_proc || !current_proc->is_user)
        return -1;

    if (access != VMA_PROT_X && access != VMA_PROT_R && access != VMA_PROT_W)
        return -1;
    uint32_t need = access;

    uint32_t va = page_floor(fault_addr);
    int vma_idx = proc_find_vma_index(current_proc, va);
    if (vma_idx < 0)
        return -1;

    const struct user_vma *v = &current_proc->vmas[vma_idx];
    if ((v->prot & need) != need)
        return -1;

    uint32_t mapped_prot = 0;
    if (vm_query_user_page(current_proc->satp, va, NULL, &mapped_prot) > 0) {
        int need_bits = prot_to_query_bits(need);
        if ((mapped_prot & (uint32_t) need_bits) == (uint32_t) need_bits)
            return 0;
        if (access == VMA_PROT_W && proc_try_cow_fault(va, v) == 0)
            return 0;
        return -1;
    }

    paddr_t pa = alloc_pages_try(1);
    if (!pa)
        return -1;
    if (vm_map_user_page(current_proc->satp, va, pa, v->prot) < 0) {
        free_pages(pa, 1);
        return -1;
    }
    return 0;
}
