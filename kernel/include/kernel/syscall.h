#pragma once

#include "common.h"

struct syscall_result {
    int value;
    // A successful exec supplies a new process context instead of a return value.
    bool context_replaced;
};

// Architecture code supplies decoded values; the dispatcher never reads registers.
struct syscall_result syscall_dispatch(uint32_t number, const uint32_t args[6]);
