#include "kernel/proc.h"
#include "kernel/usercopy.h"
#include "syscall_internal.h"

int sys_yield(void)
{
    yield();
    return 0;
}

int sys_exit(int status)
{
    proc_exit(status);
    return 0;
}

int sys_fork(void)
{
    return proc_fork();
}

int sys_exec(uint32_t entry_pc, uint32_t argv)
{
    return proc_exec(entry_pc, argv);
}

int sys_wait(uint32_t status_ptr)
{
    return sys_waitpid(-1, status_ptr, 0);
}

int sys_waitpid(int pid, uint32_t status_ptr, int options)
{
    if (status_ptr && !proc_user_writable_ok(status_ptr, sizeof(int)))
        return -1;
    int status = 0;
    int pid_result = proc_waitpid(pid, &status, options);
    if (pid_result > 0 && status_ptr &&
        copy_to_user(status_ptr, &status, sizeof(status)) < 0)
        return -1;
    return pid_result;
}
