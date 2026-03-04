#include "kernel/kernel.h"

extern char __free_ram[], __free_ram_end[];

struct page_run {
    paddr_t base;
    uint32_t pages;
    struct page_run *next;
};

static struct page_run run_nodes[PROC_MAX * 8];
static struct page_run *free_runs;
static paddr_t next_paddr = (paddr_t) __free_ram;
static bool allocator_initialized;

static void init_allocator_if_needed(void)
{
    if (allocator_initialized)
        return;

    memset(run_nodes, 0, sizeof(run_nodes));
    free_runs = NULL;
    next_paddr = (paddr_t) __free_ram;
    allocator_initialized = true;
}

static struct page_run *alloc_run_node(void)
{
    for (uint32_t i = 0; i < sizeof(run_nodes) / sizeof(run_nodes[0]); i++) {
        if (run_nodes[i].pages == 0 && run_nodes[i].next == NULL)
            return &run_nodes[i];
    }
    PANIC("out of free-run metadata");
}

static void release_run_node(struct page_run *node)
{
    node->base = 0;
    node->pages = 0;
    node->next = NULL;
}

static void coalesce_free_runs(void)
{
    struct page_run *cur = free_runs;
    while (cur && cur->next) {
        paddr_t cur_end = cur->base + cur->pages * PAGE_SIZE;
        if (cur_end == cur->next->base) {
            struct page_run *merged = cur->next;
            cur->pages += merged->pages;
            cur->next = merged->next;
            release_run_node(merged);
        } else {
            cur = cur->next;
        }
    }
}

paddr_t alloc_pages(uint32_t n)
{
    init_allocator_if_needed();
    if (n == 0)
        PANIC("alloc_pages: n must be > 0");

    struct page_run *prev = NULL;
    struct page_run *cur = free_runs;
    while (cur) {
        if (cur->pages >= n) {
            paddr_t paddr = cur->base;
            cur->base += n * PAGE_SIZE;
            cur->pages -= n;
            if (cur->pages == 0) {
                if (prev)
                    prev->next = cur->next;
                else
                    free_runs = cur->next;
                release_run_node(cur);
            }

            memset((void *) paddr, 0, n * PAGE_SIZE);
            return paddr;
        }
        prev = cur;
        cur = cur->next;
    }

    paddr_t paddr = next_paddr;
    next_paddr += n * PAGE_SIZE;

    if (next_paddr > (paddr_t) __free_ram_end)
        PANIC("out of memory");

    memset((void *) paddr, 0, n * PAGE_SIZE);
    return paddr;
}

void free_pages(paddr_t paddr, uint32_t n)
{
    init_allocator_if_needed();
    if (n == 0)
        PANIC("free_pages: n must be > 0");
    if (!is_aligned(paddr, PAGE_SIZE))
        PANIC("free_pages: paddr must be page aligned");
    uint64_t end = (uint64_t) paddr + (uint64_t) n * PAGE_SIZE;
    if (paddr < (paddr_t) __free_ram || end > (uint64_t) (paddr_t) __free_ram_end)
        PANIC("free_pages: out of managed range");

    // Bump frontier rollback: immediately reclaim most recent allocation.
    if (paddr + n * PAGE_SIZE == next_paddr) {
        next_paddr = paddr;
        return;
    }

    struct page_run *node = alloc_run_node();
    node->base = paddr;
    node->pages = n;
    node->next = NULL;

    if (!free_runs || paddr < free_runs->base) {
        if (free_runs && end > free_runs->base)
            PANIC("free_pages: overlap/double free");
        node->next = free_runs;
        free_runs = node;
    } else {
        struct page_run *cur = free_runs;
        while (cur->next && cur->next->base < paddr)
            cur = cur->next;
        if ((uint64_t) cur->base + (uint64_t) cur->pages * PAGE_SIZE > paddr)
            PANIC("free_pages: overlap/double free");
        if (cur->next && end > cur->next->base)
            PANIC("free_pages: overlap/double free");
        node->next = cur->next;
        cur->next = node;
    }

    coalesce_free_runs();
}
