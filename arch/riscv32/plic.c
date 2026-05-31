#include "kernel/plic.h"

#define PLIC_BASE 0x0c000000u
#define PLIC_PRIORITY(irq) (PLIC_BASE + ((irq) * 4u))
#define PLIC_SENABLE (PLIC_BASE + 0x2080u)
#define PLIC_SPRIORITY (PLIC_BASE + 0x201000u)
#define PLIC_SCLAIM (PLIC_BASE + 0x201004u)

static inline void plic_write(uint32_t addr, uint32_t v)
{
    *(volatile uint32_t *) addr = v;
}

static inline uint32_t plic_read(uint32_t addr)
{
    return *(volatile uint32_t *) addr;
}

void plic_init(void)
{
    // Virt machine routes virtio-mmio irqs over low source IDs.
    for (uint32_t irq = 1; irq <= 64; irq++)
        plic_write(PLIC_PRIORITY(irq), 1);

    // Enable all 64 lower sources for S-mode context0 (single hart build).
    plic_write(PLIC_SENABLE + 0, 0xffffffffu);
    plic_write(PLIC_SENABLE + 4, 0xffffffffu);

    // Accept all priorities > 0.
    plic_write(PLIC_SPRIORITY, 0);
}

uint32_t plic_claim(void)
{
    return plic_read(PLIC_SCLAIM);
}

void plic_complete(uint32_t irq)
{
    plic_write(PLIC_SCLAIM, irq);
}
