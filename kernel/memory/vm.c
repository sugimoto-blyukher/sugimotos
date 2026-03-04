#include "kernel/kernel.h"

#define SATP_MODE_SV32 (1u << 31)

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
        paddr_t l0_pa = alloc_pages(1);
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

static void vm_map_range_4k(uint32_t *root, uint32_t va, uint32_t pa, uint32_t len, uint32_t flags)
{
    uint32_t off = 0;
    while (off < len) {
        vm_map_page(root, va + off, pa + off, flags);
        off += PAGE_SIZE;
    }
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

void vm_activate(uint32_t satp_value)
{
    __asm__ __volatile__("csrw satp, %0" ::"r"(satp_value) : "memory");
    __asm__ __volatile__("sfence.vma zero, zero" ::: "memory");
}

void vm_init(void)
{
    paddr_t root_pa = alloc_pages(1);
    kernel_root_pt = (uint32_t *) root_pa;
    memset(kernel_root_pt, 0, PAGE_SIZE);

    vm_map_range_4m(kernel_root_pt, 0x02000000u, 0x02000000u, 0x00400000u, VM_FLG_R | VM_FLG_W);
    vm_map_range_4m(kernel_root_pt, 0x0c000000u, 0x0c000000u, 0x00400000u, VM_FLG_R | VM_FLG_W);
    vm_map_range_4m(kernel_root_pt, 0x10000000u, 0x10000000u, 0x02000000u, VM_FLG_R | VM_FLG_W);
    vm_map_range_4m(kernel_root_pt, 0x80000000u, 0x80000000u, 0x04000000u, VM_FLG_R | VM_FLG_W | VM_FLG_X);

    kernel_satp_value = vm_make_satp((uint32_t) root_pa);
    vm_activate(kernel_satp_value);
}

uint32_t vm_kernel_satp(void)
{
    return kernel_satp_value;
}

uint32_t vm_build_user_satp(vaddr_t user_stack_base, paddr_t user_stack_paddr, uint32_t user_stack_pages)
{
    paddr_t root_pa = alloc_pages(1);
    uint32_t *root = (uint32_t *) root_pa;
    memset(root, 0, PAGE_SIZE);
    memcpy(root, kernel_root_pt, PAGE_SIZE);

    uint32_t text_start = (uint32_t) __user_text_start & ~(PAGE_SIZE - 1u);
    uint32_t text_end = ((uint32_t) __user_text_end + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
    uint32_t ro_start = (uint32_t) __user_rodata_start & ~(PAGE_SIZE - 1u);
    uint32_t ro_end = ((uint32_t) __user_rodata_end + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
    uint32_t kro_start = (uint32_t) __rodata_start & ~(PAGE_SIZE - 1u);
    uint32_t kro_end = ((uint32_t) __rodata_end + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
    uint32_t data_start = (uint32_t) __user_data_start & ~(PAGE_SIZE - 1u);
    uint32_t data_end = ((uint32_t) __user_data_end + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
    uint32_t bss_start = (uint32_t) __user_bss_start & ~(PAGE_SIZE - 1u);
    uint32_t bss_end = ((uint32_t) __user_bss_end + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);

    if (kro_end > kro_start)
        vm_map_range_4k(root, kro_start, kro_start, kro_end - kro_start, VM_FLG_R | VM_FLG_U);
    if (text_end > text_start)
        vm_map_range_4k(root, text_start, text_start, text_end - text_start, VM_FLG_R | VM_FLG_X | VM_FLG_U);
    if (ro_end > ro_start)
        vm_map_range_4k(root, ro_start, ro_start, ro_end - ro_start, VM_FLG_R | VM_FLG_U);
    if (data_end > data_start)
        vm_map_range_4k(root, data_start, data_start, data_end - data_start, VM_FLG_R | VM_FLG_W | VM_FLG_U);
    if (bss_end > bss_start)
        vm_map_range_4k(root, bss_start, bss_start, bss_end - bss_start, VM_FLG_R | VM_FLG_W | VM_FLG_U);

    if (user_stack_pages > 0) {
        vm_map_range_4k(root,
                        user_stack_base,
                        user_stack_paddr,
                        user_stack_pages * PAGE_SIZE,
                        VM_FLG_R | VM_FLG_W | VM_FLG_U);
    }

    return vm_make_satp((uint32_t) root_pa);
}
