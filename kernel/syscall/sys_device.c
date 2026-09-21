#include "kernel/event.h"
#include "kernel/proc.h"
#include "kernel/sbi.h"
#include "kernel/usercopy.h"
#include "kernel/virtio_gpu.h"
#include "uapi/event.h"
#include "uapi/gpu.h"
#include "syscall_internal.h"

int sys_putchar(int ch)
{
    putchar((char) ch);
    return 0;
}

int sys_getchar(void)
{
    return getchar();
}

int sys_gpu_info(uint32_t user_info)
{
    struct gpu_info info = {
        .width = (uint32_t) virtio_gpu_width(),
        .height = (uint32_t) virtio_gpu_height(),
        .pitch = (uint32_t) virtio_gpu_pitch(),
        .format = GPU_FORMAT_BGRA8888,
        // The kernel backbuffer is not mapped into the user address space.
        .framebuffer = 0,
    };
    return copy_to_user(user_info, &info, sizeof(info));
}

int sys_gpu_flush(void)
{
    if (!virtio_gpu_is_ready())
        return -1;
    virtio_gpu_present();
    return 0;
}

int sys_get_event(uint32_t user_event)
{
    if (!user_event || !proc_user_writable_ok(user_event, sizeof(struct sys_event)))
        return -1;
    struct k_event event;
    int result = kevent_pop(&event);
    if (result <= 0)
        return result;
    struct sys_event out = {
        .type = event.type, .a = event.a, .b = event.b,
        .c = event.c, .d = event.d, .seq = event.seq,
    };
    if (copy_to_user(user_event, &out, sizeof(out)) < 0)
        return -1;
    return 1;
}

int sys_wmctl(int cmd, uint32_t user_arg)
{
    // Window management currently uses direct kernel calls; no argument ABI yet.
    (void) cmd;
    (void) user_arg;
    return -1;
}
