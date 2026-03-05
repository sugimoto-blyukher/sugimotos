#!/bin/bash
set -xue

QEMU=qemu-system-riscv32

CC=clang

CFLAGS="-std=c11 -O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf -fuse-ld=lld -fno-stack-protector -ffreestanding -nostdlib -Iinclude"
SOURCES=$(find kernel lib user -name '*.c' ! -path 'kernel/gui/wm.c' | sort)
DISK="${DISK:-/tmp/os_dev_fs_$$.ext4}"
MODE="${1:-gui}"
MODE_KIND="direct"
GUI_FALLBACK="${QEMU_GUI_FALLBACK:-vnc}"
QEMU_DEBUG_OPTS=()

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
    gdb)
        GDB_PORT="${QEMU_GDB_PORT:-1234}"
        QEMU_DISPLAY_OPTS=(-nographic -serial mon:stdio)
        QEMU_DEBUG_OPTS=(-S -gdb "tcp::${GDB_PORT}")
        MODE_CFLAGS=(-DUSER_INIT_AUTOSTART_GUI=0)
        ;;
    *)
        echo "Usage: $0 [gui|gui-gtk|gui-sdl|gui-vnc|cui|gdb]" >&2
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
    "$QEMU" -machine virt -bios default "${QEMU_DEBUG_OPTS[@]}" "$@" --no-reboot \
        -global virtio-mmio.force-legacy=false \
        -drive file="$DISK",if=none,id=hd0,format=raw \
        -device virtio-blk-device,drive=hd0 \
        -device virtio-gpu-device,xres=1024,yres=768 \
        -device virtio-mouse-device \
        -device virtio-tablet-device \
        -device virtio-keyboard-device \
        -kernel kernel.elf
}

has_graphical_session() {
    [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]
}

run_qemu_with_errlog() {
    local errlog="$1"
    shift
    set +e
    run_qemu "$@" 2> >(tee "$errlog" >&2)
    local rc=$?
    set -e
    return "$rc"
}

launch_cui() {
    echo "run.sh: starting CUI mode" >&2
    run_qemu -nographic -serial mon:stdio
}

launch_vnc() {
    local vnc_bind="${QEMU_VNC_BIND:-127.0.0.1}"
    local vnc_display="${QEMU_VNC_DISPLAY:-1}"
    echo "run.sh: starting VNC display ${vnc_bind}:${vnc_display}" >&2
    echo "run.sh: connect with a VNC client to ${vnc_bind}:${vnc_display}" >&2
    run_qemu -display none -vnc "${vnc_bind}:${vnc_display}" -serial mon:stdio -monitor none
}

launch_headless_fallback() {
    if [[ "$GUI_FALLBACK" == "cui" ]]; then
        launch_cui
    else
        launch_vnc
    fi
}

if [[ "$MODE" == "gui-vnc" ]]; then
    echo "run.sh: starting VNC display ${VNC_BIND}:${VNC_DISPLAY}" >&2
    echo "run.sh: connect with a VNC client to ${VNC_BIND}:${VNC_DISPLAY}" >&2
fi

if [[ "$MODE" == "gdb" ]]; then
    echo "run.sh: QEMU waiting for GDB on tcp::${GDB_PORT}" >&2
    echo "run.sh: attach with: riscv64-unknown-elf-gdb -x tools/gdbinit-riscv32.gdb" >&2
fi

if [[ "$MODE_KIND" == "auto-gui" ]]; then
    if ! has_graphical_session; then
        echo "run.sh: no DISPLAY/WAYLAND detected; using headless fallback (${GUI_FALLBACK})" >&2
        launch_headless_fallback
        exit $?
    fi

    ERRLOG="/tmp/qemu_gui_${$}.err"
    RC2=0
    run_qemu_with_errlog "$ERRLOG" "${QEMU_DISPLAY_OPTS[@]}"
    RC=$?
    if [[ $RC -ne 0 ]] && grep -Eiq "display output is not active|gtk initialization failed" "$ERRLOG"; then
        echo "run.sh: GTK backend failed; retrying with SDL" >&2
        ERRLOG2="/tmp/qemu_sdl_${$}.err"
        run_qemu_with_errlog "$ERRLOG2" -display sdl,gl=off -serial mon:stdio -monitor none
        RC2=$?
        if [[ $RC2 -ne 0 ]] && grep -Eiq "display output is not active|could not initialize SDL|x11 not available|wayland not available" "$ERRLOG2"; then
            echo "run.sh: GUI backend unavailable; using headless fallback (${GUI_FALLBACK})" >&2
            launch_headless_fallback
        else
            exit $RC2
        fi
    else
        exit $RC
    fi
else
    run_qemu "${QEMU_DISPLAY_OPTS[@]}"
fi
