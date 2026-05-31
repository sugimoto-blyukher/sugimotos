#!/bin/bash
set -xue

QEMU=qemu-system-riscv32
CC=clang
CFLAGS="-std=c11 -O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf -fuse-ld=lld -fno-stack-protector -ffreestanding -nostdlib -Iinclude"

# Build the kernel
$CC $CFLAGS -Wl,-Tkernel/arch/riscv32/kernel.ld -Wl,-Map=kernel.map -o kernel.elf \
    $(find kernel lib -name "*.c")

# Run QEMU
$QEMU -machine virt -bios opensbi-riscv32-generic-fw_dynamic.bin -kernel kernel.elf -serial stdio \
    -drive if=none,format=raw,file=fs.ext4,id=hd0 \
    -device virtio-blk-device,drive=hd0 \
    -device virtio-gpu-device \
    -device virtio-keyboard-device \
    -device virtio-mouse-device \
    -global virtio-mmio.force-legacy=false \
    -display sdl 