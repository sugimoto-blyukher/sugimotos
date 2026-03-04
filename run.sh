#!/bin/bash
set -xue

QEMU=qemu-system-riscv32

CC=clang

CFLAGS="-std=c11 -O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf -fuse-ld=lld -fno-stack-protector -ffreestanding -nostdlib -Iinclude"
SOURCES=$(find kernel lib -name '*.c' | sort)
DISK=fs.ext4
MODE="${1:-gui}"

case "$MODE" in
    gui)
        QEMU_DISPLAY_OPTS=(-display gtk,grab-on-hover=on -serial vc -monitor none)
        MODE_CFLAGS=(-DSHELL_START_IN_GUI=1)
        ;;
    cui)
        QEMU_DISPLAY_OPTS=(-nographic -serial mon:stdio)
        MODE_CFLAGS=(-DSHELL_START_IN_GUI=0)
        ;;
    *)
        echo "Usage: $0 [gui|cui]" >&2
        exit 1
        ;;
esac

# カーネルをビルド
$CC $CFLAGS -Wl,-Tkernel/arch/riscv32/kernel.ld -Wl,-Map=kernel.map -o kernel.elf \
    "${MODE_CFLAGS[@]}" \
    $SOURCES

# ext4ディスクイメージを作成
dd if=/dev/zero of=$DISK bs=1M count=16
mkfs.ext4 -q -F $DISK
echo "hello from ext4" > /tmp/ext4_hello.txt
echo "this file lives on ext4" > /tmp/ext4_note.txt
debugfs -w -R "write /tmp/ext4_hello.txt /ext_hello.txt" $DISK > /dev/null
debugfs -w -R "write /tmp/ext4_note.txt /ext_note.txt" $DISK > /dev/null

# QEMUを起動
$QEMU -machine virt -bios default "${QEMU_DISPLAY_OPTS[@]}" --no-reboot \
    -global virtio-mmio.force-legacy=false \
    -drive file=$DISK,if=none,id=hd0,format=raw \
    -device virtio-blk-device,drive=hd0 \
    -device virtio-gpu-device \
    -device virtio-mouse-device \
    -kernel kernel.elf
