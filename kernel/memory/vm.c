#include "kernel/kernel.h"

#define SATP_MODE_SV32 (1u << 31)
#define SATP_PPN_MASK 0x003fffffu

#define PTE_V (1u << 0)
#define PTE_R (1u << 1)
#define PTE_W (1u << 2)
#define PTE_X (1u << 3)
#define PTE_U (1u << 4)
#define PTE_A (1u << 6)
#define PTE_D (1u << 7)

#define VM_FLG_R 1u
#define VM_FLG_W 2u
#define VM_FLG_X 4u
#define VM_FLG_U 8u

extern char __user_text_start[], __user_text_end[];
extern char __user_rodata_start[], __user_rodata_end[];
extern char __user_data_start[], __user_data_end[];
extern char __user_bss_start[], __user_bss_end[];
extern char __rodata_start[], __rodata_end[];

static uint32_t *kernel_root_pt;
static uint32_t kernel_satp_value;

static uint32_t page_floor(uint32_t value)
{
    return value & ~(PAGE_SIZE - 1u);
}

static uint32_t page_ceil(uint32_t value)
{
    return (value + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
}

static uint32_t pte_from_pa(uint32_t pa, uint32_t perm)
{
    return ((pa >> 12) << 10) | perm | PTE_V | PTE_A | PTE_D;
}

static uint32_t pte_perm_from_flags(uint32_t flags)
{
    uint32_t p = 0;
    if (flags & VM_FLG_R)
        p |= PTE_R;
    if (flags & VM_FLG_W)
        p |= PTE_W;
    if (flags & VM_FLG_X)
        p |= PTE_X;
    if (flags & VM_FLG_U)
        p |= PTE_U;
    return p;
}

static int vm_map_page(uint32_t *root, uint32_t va, uint32_t pa, uint32_t flags)
{
    uint32_t vpn1 = (va >> 22) & 0x3ffu;
    uint32_t vpn0 = (va >> 12) & 0x3ffu;
    uint32_t pte1 = root[vpn1];
    uint32_t *l0;

    if (!(pte1 & PTE_V) || (pte1 & (PTE_R | PTE_W | PTE_X))) {
        paddr_t l0_pa = alloc_pages_try(1);
        if (!l0_pa)
            return -1;
        memset((void *) l0_pa, 0, PAGE_SIZE);
        if ((pte1 & PTE_V) && (pte1 & (PTE_R | PTE_W | PTE_X))) {
            uint32_t perm = pte1 & (PTE_R | PTE_W | PTE_X | PTE_U | PTE_A | PTE_D);
            uint32_t pa4m = ((pte1 >> 10) << 12) & ~0x003fffffu;
            uint32_t *init_l0 = (uint32_t *) l0_pa;
            for (uint32_t i = 0; i < 1024; i++)
                init_l0[i] = pte_from_pa(pa4m + i * PAGE_SIZE, perm);
        }
        root[vpn1] = ((l0_pa >> 12) << 10) | PTE_V;
        l0 = (uint32_t *) l0_pa;
    } else {
        l0 = (uint32_t *) ((pte1 >> 10) << 12);
    }

    l0[vpn0] = pte_from_pa(pa, pte_perm_from_flags(flags));
    return 0;
}

static int vm_map_range_4k(uint32_t *root, uint32_t va, uint32_t pa, uint32_t len, uint32_t flags)
{
    uint32_t off = 0;
    while (off < len) {
        if (vm_map_page(root, va + off, pa + off, flags) < 0)
            return -1;
        off += PAGE_SIZE;
    }
    return 0;
}

static void vm_map_range_4m(uint32_t *root, uint32_t va, uint32_t pa, uint32_t len, uint32_t flags)
{
    uint32_t perm = pte_perm_from_flags(flags);
    uint32_t off = 0;
    while (off < len) {
        uint32_t cur_va = va + off;
        uint32_t cur_pa = pa + off;
        uint32_t vpn1 = (cur_va >> 22) & 0x3ffu;
        root[vpn1] = pte_from_pa(cur_pa, perm);
        off += 0x400000u;
    }
}

static uint32_t vm_make_satp(uint32_t root_pa)
{
    return SATP_MODE_SV32 | (root_pa >> 12);
}

static uint32_t *vm_root_from_satp(uint32_t satp_value)
{
    uint32_t ppn = satp_value & SATP_PPN_MASK;
    return (uint32_t *) (ppn << 12);
}

static int vm_map_identity_segment(uint32_t *root, uint32_t start, uint32_t end, uint32_t flags)
{
    if (end <= start)
        return 0;
    return vm_map_range_4k(root, start, start, end - start, flags);
}

void vm_activate(uint32_t satp_value)
{
    __asm__ __volatile__("csrw satp, %0" ::"r"(satp_value) : "memory");
    __asm__ __volatile__("sfence.vma zero, zero" ::: "memory");
}

int vm_query_user_page(uint32_t satp_value, uint32_t va, paddr_t *pa_out, uint32_t *prot_out)
{
    uint32_t *root = vm_root_from_satp(satp_value);
    uint32_t vpn1 = (va >> 22) & 0x3ffu;
    uint32_t vpn0 = (va >> 12) & 0x3ffu;
    uint32_t pte1 = root[vpn1];
    if (!(pte1 & PTE_V))
        return 0;

    if (pte1 & (PTE_R | PTE_W | PTE_X)) {
        uint32_t pa = ((pte1 >> 10) << 12) | (va & 0x003fffffu);
        if (pa_out)
            *pa_out = pa;
        if (prot_out)
            *prot_out = pte1 & (PTE_R | PTE_W | PTE_X | PTE_U);
        return 1;
    }

    uint32_t *l0 = (uint32_t *) ((pte1 >> 10) << 12);
    uint32_t pte0 = l0[vpn0];
    if (!(pte0 & PTE_V) || !(pte0 & (PTE_R | PTE_W | PTE_X)))
        return 0;

    if (pa_out)
        *pa_out = ((pte0 >> 10) << 12) | (va & 0xfffu);
    if (prot_out)
        *prot_out = pte0 & (PTE_R | PTE_W | PTE_X | PTE_U);
    return 1;
}

int vm_map_user_page(uint32_t satp_value, uint32_t va, uint32_t pa, uint32_t prot)
{
    if (!is_aligned(va, PAGE_SIZE) || !is_aligned(pa, PAGE_SIZE))
        return -1;
    uint32_t *root = vm_root_from_satp(satp_value);
    uint32_t flags = 0;
    if (prot & VMA_PROT_R)
        flags |= VM_FLG_R;
    if (prot & VMA_PROT_W)
        flags |= VM_FLG_W;
    if (prot & VMA_PROT_X)
        flags |= VM_FLG_X;
    flags |= VM_FLG_U;
    return vm_map_page(root, va, pa, flags);
}

int vm_unmap_user_page(uint32_t satp_value, uint32_t va)
{
    if (!is_aligned(va, PAGE_SIZE))
        return -1;

    uint32_t *root = vm_root_from_satp(satp_value);
    uint32_t vpn1 = (va >> 22) & 0x3ffu;
    uint32_t vpn0 = (va >> 12) & 0x3ffu;
    uint32_t pte1 = root[vpn1];
    if (!(pte1 & PTE_V))
        return 0;
    if (pte1 & (PTE_R | PTE_W | PTE_X))
        return -1;

    uint32_t *l0 = (uint32_t *) ((pte1 >> 10) << 12);
    l0[vpn0] = 0;
    __asm__ __volatile__("sfence.vma zero, zero" ::: "memory");
    return 0;
}

int vm_init(void)
{
    paddr_t root_pa = alloc_pages_try(1);
    if (!root_pa)
        return -1;
    kernel_root_pt = (uint32_t *) root_pa;
    memset(kernel_root_pt, 0, PAGE_SIZE);

    vm_map_range_4m(kernel_root_pt, 0x02000000u, 0x02000000u, 0x00400000u, VM_FLG_R | VM_FLG_W);
    vm_map_range_4m(kernel_root_pt, 0x0c000000u, 0x0c000000u, 0x00400000u, VM_FLG_R | VM_FLG_W);
    vm_map_range_4m(kernel_root_pt, 0x10000000u, 0x10000000u, 0x02000000u, VM_FLG_R | VM_FLG_W);
    vm_map_range_4m(kernel_root_pt, 0x80000000u, 0x80000000u, 0x04000000u, VM_FLG_R | VM_FLG_W | VM_FLG_X);

    kernel_satp_value = vm_make_satp((uint32_t) root_pa);
    vm_activate(kernel_satp_value);
    return 0;
}

uint32_t vm_kernel_satp(void)
{
    return kernel_satp_value;
}

uint32_t vm_build_user_satp(vaddr_t user_stack_base, paddr_t user_stack_paddr, uint32_t user_stack_pages)
{
    paddr_t root_pa = alloc_pages_try(1);
    if (!root_pa)
        return 0;
    uint32_t *root = (uint32_t *) root_pa;
    memset(root, 0, PAGE_SIZE);
    memcpy(root, kernel_root_pt, PAGE_SIZE);

    struct vm_identity_segment {
        uint32_t start;
        uint32_t end;
        uint32_t flags;
    } segments[] = {
        {page_floor((uint32_t) __rodata_start), page_ceil((uint32_t) __rodata_end), VM_FLG_R | VM_FLG_U},
        {page_floor((uint32_t) __user_text_start),
         page_ceil((uint32_t) __user_text_end),
         VM_FLG_R | VM_FLG_X | VM_FLG_U},
        {page_floor((uint32_t) __user_rodata_start),
         page_ceil((uint32_t) __user_rodata_end),
         VM_FLG_R | VM_FLG_U},
        {page_floor((uint32_t) __user_data_start),
         page_ceil((uint32_t) __user_data_end),
         VM_FLG_R | VM_FLG_W | VM_FLG_U},
        {page_floor((uint32_t) __user_bss_start),
         page_ceil((uint32_t) __user_bss_end),
         VM_FLG_R | VM_FLG_W | VM_FLG_U},
    };

    for (size_t i = 0; i < sizeof(segments) / sizeof(segments[0]); i++) {
        if (vm_map_identity_segment(root, segments[i].start, segments[i].end, segments[i].flags) < 0)
            goto fail;
    }

    if (user_stack_pages > 0) {
        if (vm_map_range_4k(root,
                            user_stack_base,
                            user_stack_paddr,
                            user_stack_pages * PAGE_SIZE,
                            VM_FLG_R | VM_FLG_W | VM_FLG_U) < 0)
            goto fail;
    }

    return vm_make_satp((uint32_t) root_pa);

fail:
    free_pages(root_pa, 1);
    return 0;
}
