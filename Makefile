# Makefile

# ツール
CC         = gcc
LD         = ld
NASM       = nasm
QEMU       = qemu-system-x86_64

# ビルドオプション
CFLAGS     = -ffreestanding -m32 -c -Wall -Wextra
LDFLAGS    = -m elf_i386
NFLAGS     = -f bin
QEMU_FLAGS = -monitor stdio

# ソースファイル
SOURCES    = $(wildcard *.c)

# オブジェクトファイル
OBJECTS    = $(SOURCES:.c=.o)

# 出力ファイル
BOOT       = boot.bin
KERNEL     = kernel.bin
KERNEL_ELF = kernel.elf
IMG        = disk.img

# デフォルトターゲット
all: $(IMG)

# ディスクイメージ作成
$(IMG): $(BOOT) $(KERNEL)
	cat $^ > $@

# カーネルのリンク
$(KERNEL_ELF): $(OBJECTS) kernel.ld
	$(LD) $(LDFLAGS) -T kernel.ld $(OBJECTS) -o $@

# カーネルELFからバイナリへ
$(KERNEL): $(KERNEL_ELF)
	objcopy -O binary $< $@

# Cファイルからオブジェクトへの変換ルール
%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

# アセンブリからバイナリへの変換ルール
%.bin: %.asm
	$(NASM) $(NFLAGS) $< -o $@

# QEMUでのエミュレーション実行
run: $(IMG)
	$(QEMU) $(QEMU_FLAGS) -fda $<

# クリーンアップ
clean:
	rm -f $(IMG) $(BOOT) $(KERNEL) $(KERNEL_ELF) $(OBJECTS)

# 偽ターゲットの定義
.PHONY: all run clean