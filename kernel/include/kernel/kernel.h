#pragma once

#include "arch/csr.h"
#include "arch/trap.h"
#include "kernel/fs.h"
#include "kernel/page_alloc.h"
#include "kernel/panic.h"
#include "kernel/proc.h"
#include "kernel/sbi.h"
#include "kernel/vm.h"

void kernel_main(void);
void boot(void);
