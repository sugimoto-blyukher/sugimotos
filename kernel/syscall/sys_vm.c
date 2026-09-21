#include "kernel/proc.h"
#include "syscall_internal.h"

int sys_mmap(uint32_t user_addr, uint32_t length, uint32_t prot, uint32_t flags)
{
    return proc_mmap(user_addr, length, prot, flags);
}

int sys_munmap(uint32_t user_addr, uint32_t length)
{
    return proc_munmap(user_addr, length);
}
