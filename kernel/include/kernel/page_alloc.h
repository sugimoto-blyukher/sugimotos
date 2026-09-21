#pragma once

#include "common.h"

paddr_t alloc_pages(uint32_t n);
paddr_t alloc_pages_try(uint32_t n);
void free_pages(paddr_t paddr, uint32_t n);
void page_inc_ref(paddr_t paddr);
void page_dec_ref(paddr_t paddr);
