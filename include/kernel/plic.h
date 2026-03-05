#pragma once

#include "common.h"

void plic_init(void);
uint32_t plic_claim(void);
void plic_complete(uint32_t irq);
