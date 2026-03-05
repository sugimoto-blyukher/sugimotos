#!/bin/bash
set -xue

QEMU=qemu-system-riscv32

CC=clang

CFLAGS="-std=c11 -O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf -fuse-ld=lld -fno-stack-protector -ffreestanding -nostdlib -Iinclude"
SOURCES=$(find kernel lib user -name '*.c' ! -path 'kernel/gui/wm.c' | sort)
DISK="${DISK:-/tmp/os_dev_fs_$$.ext4}"
MODE="${1:-gui}"
MODE_KIND="direct"

case "$MODE" in
    gui)
        QEMU_DISPLAY_OPTS=(-display gtk,gl=off,grab-on-hover=on -serial mon:stdio -monitor none)
        MODE_CFLAGS=(-DUSER_INIT_AUTOSTART_GUI=1)
        MODE_KIND="auto-gui"
        ;;
    gui-gtk)
        QEMU_DISPLAY_OPTS=(-display gtk,gl=off,grab-on-hover=on -serial mon:stdio -monitor none)
        MODE_CFLAGS=(-DUSER_INIT_AUTOSTART_GUI=1)
        ;;
    gui-sdl)
        QEMU_DISPLAY_OPTS=(-display sdl,gl=off -serial mon:stdio -monitor none)
        MODE_CFLAGS=(-DUSER_INIT_AUTOSTART_GUI=1)
        ;;
    gui-vnc)
        VNC_BIND="${QEMU_VNC_BIND:-127.0.0.1}"
        VNC_DISPLAY="${QEMU_VNC_DISPLAY:-1}"
        QEMU_DISPLAY_OPTS=(-display none -vnc "${VNC_BIND}:${VNC_DISPLAY}" -serial mon:stdio -monitor none)
        MODE_CFLAGS=(-DUSER_INIT_AUTOSTART_GUI=1)
        ;;
    cui)
        QEMU_DISPLAY_OPTS=(-nographic -serial mon:stdio)
        MODE_CFLAGS=(-DUSER_INIT_AUTOSTART_GUI=0)
        ;;
    *)
        echo "Usage: $0 [gui|gui-gtk|gui-sdl|gui-vnc|cui]" >&2
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
run_qemu() {
    "$QEMU" -machine virt -bios default "$@" --no-reboot \
        -global virtio-mmio.force-legacy=false \
        -drive file="$DISK",if=none,id=hd0,format=raw \
        -device virtio-blk-device,drive=hd0 \
        -device virtio-gpu-device,xres=1024,yres=768 \
        -device virtio-mouse-device \
        -device virtio-tablet-device \
        -device virtio-keyboard-device \
        -kernel kernel.elf
}

if [[ "$MODE" == "gui-vnc" ]]; then
    echo "run.sh: starting VNC display ${VNC_BIND}:${VNC_DISPLAY}" >&2
    echo "run.sh: connect with a VNC client to ${VNC_BIND}:${VNC_DISPLAY}" >&2
fi

if [[ "$MODE_KIND" == "auto-gui" ]]; then
    ERRLOG="/tmp/qemu_gui_${$}.err"
    RC2=0
    set +e
    run_qemu "${QEMU_DISPLAY_OPTS[@]}" 2> >(tee "$ERRLOG" >&2)
    RC=$?
    set -e
    if [[ $RC -ne 0 ]] && grep -Eiq "display output is not active|gtk initialization failed" "$ERRLOG"; then
        echo "run.sh: GTK backend failed; retrying with SDL" >&2
        ERRLOG2="/tmp/qemu_sdl_${$}.err"
        set +e
        run_qemu -display sdl,gl=off -serial mon:stdio -monitor none 2> >(tee "$ERRLOG2" >&2)
        RC2=$?
        set -e
        if [[ $RC2 -ne 0 ]] && grep -Eiq "display output is not active|could not initialize SDL|x11 not available|wayland not available" "$ERRLOG2"; then
            FALLBACK="vnc"
            if [[ "$FALLBACK" == "cui" ]]; then
                echo "run.sh: GUI backend unavailable; retrying in CUI mode (QEMU_GUI_FALLBACK=cui)" >&2
                run_qemu -nographic -serial mon:stdio
            else
                VNC_BIND="${QEMU_VNC_BIND:-127.0.0.1}"
                VNC_DISPLAY="${QEMU_VNC_DISPLAY:-1}"
                echo "run.sh: GUI backend unavailable; starting VNC display ${VNC_BIND}:${VNC_DISPLAY}" >&2
                echo "run.sh: connect with a VNC client to ${VNC_BIND}:${VNC_DISPLAY}" >&2
                ERRLOG3="/tmp/qemu_vnc_${$}.err"
                RC3=0
                set +e
                run_qemu -display none -vnc "${VNC_BIND}:${VNC_DISPLAY}" -serial mon:stdio -monitor none 2> >(tee "$ERRLOG3" >&2)
                RC3=$?
                set -e
                if [[ $RC3 -ne 0 ]]; then
                    echo "run.sh: VNC backend failed; retrying in CUI mode" >&2
                    run_qemu -nographic -serial mon:stdio
                else
                    exit 0
                fi
            fi
        else
            exit $RC2
        fi
    else
        exit $RC
    fi
else
    run_qemu "${QEMU_DISPLAY_OPTS[@]}"
fi
