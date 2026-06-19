# Sugimotos Makefile
# Existing run.sh compatible build system

TARGET := kernel.elf
MAP    := kernel.map

QEMU := qemu-system-riscv32
CC   := clang

ARCH := riscv32
LINKER_SCRIPT := arch/$(ARCH)/kernel.ld

CFLAGS := \
	-std=c11 \
	-O2 \
	-g3 \
	-Wall \
	-Wextra \
	--target=riscv32-unknown-elf \
	-fuse-ld=lld \
	-fno-stack-protector \
	-ffreestanding \
	-nostdlib \
	-Iarch/riscv32/include \
	-Ikernel/include

LDFLAGS := \
	-Wl,-T$(LINKER_SCRIPT) \
	-Wl,-Map=$(MAP)

SRCS := $(shell find arch/riscv32 drivers fs kernel lib -name "*.c")

QEMUFLAGS := \
	-machine virt \
	-bios opensbi-riscv32-generic-fw_dynamic.bin \
	-kernel $(TARGET) \
	-drive if=none,format=raw,file=fs.ext4,id=hd0 \
	-device virtio-blk-device,drive=hd0 \
	-global virtio-mmio.force-legacy=false \
	-nographic

.PHONY: all build run clean debug print-sources

all: build

build: $(TARGET)

$(TARGET): $(SRCS) $(LINKER_SCRIPT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS)

run: $(TARGET)
	$(QEMU) $(QEMUFLAGS)

debug: $(TARGET)
	$(QEMU) $(QEMUFLAGS) -S -s

print-sources:
	@printf '%s\n' $(SRCS)

clean:
	rm -f $(TARGET) $(MAP)
