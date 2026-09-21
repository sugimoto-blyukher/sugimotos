#pragma once

#include "common.h"

int vm_init(void);
uint32_t vm_kernel_satp(void);
uint32_t vm_build_user_satp(vaddr_t user_stack_base, paddr_t user_stack_paddr, uint32_t user_stack_pages);
void vm_activate(uint32_t satp_value);
int vm_map_user_page(uint32_t satp_value, uint32_t va, uint32_t pa, uint32_t prot);
int vm_unmap_user_page(uint32_t satp_value, uint32_t va);
int vm_query_user_page(uint32_t satp_value, uint32_t va, paddr_t *pa_out, uint32_t *prot_out);
