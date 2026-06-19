#!/bin/bash
set -xue

QEMU=qemu-system-riscv32
CC=clang
CFLAGS="-std=c11 -O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf -fuse-ld=lld -fno-stack-protector -ffreestanding -nostdlib -Iarch/riscv32/include -Ikernel/include"

# Build the kernel
$CC $CFLAGS -Wl,-Tarch/riscv32/kernel.ld -Wl,-Map=kernel.map -o kernel.elf \
    $(find arch/riscv32 drivers fs kernel lib -name "*.c")

# Run QEMU
$QEMU -machine virt -bios opensbi-riscv32-generic-fw_dynamic.bin -kernel kernel.elf \
    -drive if=none,format=raw,file=fs.ext4,id=hd0 \
    -device virtio-blk-device,drive=hd0 \
    -global virtio-mmio.force-legacy=false \
    -nographic
