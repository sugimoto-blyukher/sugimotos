#include "kernel/kernel.h"
#include "kernel/lock.h"

extern char __free_ram[], __free_ram_end[];

struct page_run {
    paddr_t base;
    uint32_t pages;
    struct page_run *next;
};

static struct page_run *free_runs;
static struct page_run *free_meta_nodes;
static paddr_t next_paddr = (paddr_t) __free_ram;
static paddr_t meta_top = (paddr_t) __free_ram_end;
static bool allocator_initialized;
static struct spinlock alloc_lock;
static uint16_t *page_refcnt;
static uint32_t managed_page_count;
static int free_meta_oom_warned;

static int init_allocator_if_needed(void)
{
    if (allocator_initialized)
        return 0;

    free_runs = NULL;
    free_meta_nodes = NULL;
    next_paddr = (paddr_t) __free_ram;
    meta_top = (paddr_t) __free_ram_end;
    managed_page_count = ((paddr_t) __free_ram_end - (paddr_t) __free_ram) / PAGE_SIZE;
    paddr_t ref_bytes = managed_page_count * (paddr_t) sizeof(uint16_t);
    paddr_t ref_top = (meta_top - ref_bytes) & ~(paddr_t) (sizeof(uint32_t) - 1u);
    if (ref_top <= next_paddr)
        return -1;
    meta_top = ref_top;
    page_refcnt = (uint16_t *) meta_top;
    memset(page_refcnt, 0, managed_page_count * sizeof(uint16_t));
    allocator_initialized = true;
    return 0;
}

static int page_idx_if_managed(paddr_t paddr, uint32_t *idx_out)
{
    if (paddr < (paddr_t) __free_ram || paddr >= (paddr_t) __free_ram_end)
        return 0;
    if (!is_aligned(paddr, PAGE_SIZE))
        return 0;
    uint32_t idx = (paddr - (paddr_t) __free_ram) / PAGE_SIZE;
    if (idx >= managed_page_count)
        return 0;
    if (idx_out)
        *idx_out = idx;
    return 1;
}

static struct page_run *alloc_run_node(int fail_ok)
{
    if (free_meta_nodes) {
        struct page_run *n = free_meta_nodes;
        free_meta_nodes = free_meta_nodes->next;
        n->next = NULL;
        return n;
    }

    paddr_t new_top = meta_top - (paddr_t) sizeof(struct page_run);
    new_top &= ~(paddr_t) (sizeof(uint32_t) - 1u);
    (void) fail_ok;
    if (new_top <= next_paddr)
        return NULL;
    meta_top = new_top;
    struct page_run *n = (struct page_run *) meta_top;
    memset(n, 0, sizeof(*n));
    return n;
}

static void release_run_node(struct page_run *node)
{
    node->base = 0;
    node->pages = 0;
    node->next = free_meta_nodes;
    free_meta_nodes = node;
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

static int free_single_page_raw(paddr_t paddr)
{
    uint64_t end = (uint64_t) paddr + PAGE_SIZE;
    if (!free_runs) {
        struct page_run *node = alloc_run_node(1);
        if (!node)
            return -1;
        node->base = paddr;
        node->pages = 1;
        node->next = NULL;
        free_runs = node;
        return 0;
    }

    if (paddr < free_runs->base) {
        if (end > free_runs->base)
            PANIC("free_pages: overlap/double free");
        if (end == free_runs->base) {
            free_runs->base = paddr;
            free_runs->pages++;
            return 0;
        }
        struct page_run *node = alloc_run_node(1);
        if (!node)
            return -1;
        node->base = paddr;
        node->pages = 1;
        node->next = free_runs;
        free_runs = node;
        return 0;
    }

    struct page_run *cur = free_runs;
    while (cur->next && cur->next->base < paddr)
        cur = cur->next;
    if ((uint64_t) cur->base + (uint64_t) cur->pages * PAGE_SIZE > paddr)
        PANIC("free_pages: overlap/double free");
    if (cur->next && end > cur->next->base)
        PANIC("free_pages: overlap/double free");

    uint64_t cur_end = (uint64_t) cur->base + (uint64_t) cur->pages * PAGE_SIZE;
    if (cur_end == paddr) {
        cur->pages++;
        if (cur->next && ((uint64_t) cur->base + (uint64_t) cur->pages * PAGE_SIZE) == cur->next->base) {
            struct page_run *next = cur->next;
            cur->pages += next->pages;
            cur->next = next->next;
            release_run_node(next);
        }
        return 0;
    }
    if (cur->next && end == cur->next->base) {
        cur->next->base = paddr;
        cur->next->pages++;
        return 0;
    }

    struct page_run *node = alloc_run_node(1);
    if (!node)
        return -1;
    node->base = paddr;
    node->pages = 1;
    node->next = cur->next;
    cur->next = node;
    coalesce_free_runs();
    return 0;
}

paddr_t alloc_pages_try(uint32_t n)
{
    spin_lock(&alloc_lock);
    if (init_allocator_if_needed() < 0) {
        spin_unlock(&alloc_lock);
        return 0;
    }
    if (n == 0) {
        spin_unlock(&alloc_lock);
        return 0;
    }

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
            for (uint32_t i = 0; i < n; i++) {
                uint32_t idx = 0;
                if (page_idx_if_managed(paddr + i * PAGE_SIZE, &idx))
                    page_refcnt[idx] = 1;
            }
            spin_unlock(&alloc_lock);
            return paddr;
        }
        prev = cur;
        cur = cur->next;
    }

    paddr_t paddr = next_paddr;
    next_paddr += n * PAGE_SIZE;

    if (next_paddr > meta_top) {
        spin_unlock(&alloc_lock);
        return 0;
    }

    memset((void *) paddr, 0, n * PAGE_SIZE);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx = 0;
        if (page_idx_if_managed(paddr + i * PAGE_SIZE, &idx))
            page_refcnt[idx] = 1;
    }
    spin_unlock(&alloc_lock);
    return paddr;
}

paddr_t alloc_pages(uint32_t n)
{
    return alloc_pages_try(n);
}

void free_pages(paddr_t paddr, uint32_t n)
{
    spin_lock(&alloc_lock);
    if (init_allocator_if_needed() < 0) {
        spin_unlock(&alloc_lock);
        PANIC("allocator unavailable");
    }
    if (n == 0) {
        spin_unlock(&alloc_lock);
        PANIC("free_pages: n must be > 0");
    }
    if (!is_aligned(paddr, PAGE_SIZE)) {
        spin_unlock(&alloc_lock);
        PANIC("free_pages: paddr must be page aligned");
    }
    uint64_t end = (uint64_t) paddr + (uint64_t) n * PAGE_SIZE;
    if (paddr < (paddr_t) __free_ram || end > (uint64_t) (paddr_t) __free_ram_end) {
        spin_unlock(&alloc_lock);
        PANIC("free_pages: out of managed range");
    }

    for (uint32_t i = 0; i < n; i++) {
        paddr_t cur = paddr + i * PAGE_SIZE;
        uint32_t idx = 0;
        if (!page_idx_if_managed(cur, &idx))
            continue;
        if (page_refcnt[idx] == 0) {
            spin_unlock(&alloc_lock);
            PANIC("free_pages: double free");
        }
        page_refcnt[idx]--;
        if (page_refcnt[idx] == 0 && free_single_page_raw(cur) < 0) {
            page_refcnt[idx] = 1;
            if (!free_meta_oom_warned) {
                free_meta_oom_warned = 1;
                printf("warn: allocator metadata exhausted; page leak fallback\n");
            }
        }
    }
    spin_unlock(&alloc_lock);
}

void page_inc_ref(paddr_t paddr)
{
    spin_lock(&alloc_lock);
    if (init_allocator_if_needed() < 0) {
        spin_unlock(&alloc_lock);
        PANIC("allocator unavailable");
    }
    uint32_t idx = 0;
    if (page_idx_if_managed(paddr, &idx)) {
        if (page_refcnt[idx] == 0) {
            spin_unlock(&alloc_lock);
            PANIC("page_inc_ref on free page");
        }
        page_refcnt[idx]++;
    }
    spin_unlock(&alloc_lock);
}

void page_dec_ref(paddr_t paddr)
{
    uint32_t idx = 0;
    spin_lock(&alloc_lock);
    if (init_allocator_if_needed() < 0) {
        spin_unlock(&alloc_lock);
        PANIC("allocator unavailable");
    }
    if (!page_idx_if_managed(paddr, &idx)) {
        spin_unlock(&alloc_lock);
        return;
    }
    if (page_refcnt[idx] == 0) {
        spin_unlock(&alloc_lock);
        PANIC("page_dec_ref on free page");
    }
    page_refcnt[idx]--;
    if (page_refcnt[idx] == 0 && free_single_page_raw(paddr) < 0) {
        page_refcnt[idx] = 1;
        if (!free_meta_oom_warned) {
            free_meta_oom_warned = 1;
            printf("warn: allocator metadata exhausted; page leak fallback\n");
        }
    }
    spin_unlock(&alloc_lock);
}
