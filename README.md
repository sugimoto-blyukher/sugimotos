# Sugimotos

AI支援を使いつつ、現在は各機能の動作原理を再読解・再実装中。

RISC-V 32bit 向けの自作 OS / モノリシックカーネルです。
QEMU の `virt` マシン上で OpenSBI 経由で起動し、virtio-blk / virtio-gpu / virtio-keyboard / virtio-mouse を使います。

## 必要な依存パッケージ

Ubuntu / Debian 系では、以下をインストールしてください。

```sh
sudo apt update
sudo apt install clang lld qemu-system-misc qemu-system-gui
```

必要なコマンドは次の通りです。

- `clang`: RISC-V 32bit 向けにカーネルをクロスコンパイルします。
- `ld.lld`: `clang -fuse-ld=lld` から使用されるリンカです。
- `qemu-system-riscv32`: カーネルを実行する RISC-V エミュレータです。
- SDL 対応の QEMU GUI: `run.sh` は `-display sdl` で起動します。

このリポジトリには起動に必要な `opensbi-riscv32-generic-fw_dynamic.bin` と、virtio-blk 用の `fs.ext4` が含まれています。

## ビルド方法

通常は `run.sh` がビルドと起動をまとめて行います。

ビルドだけ確認したい場合は、以下を実行してください。

```sh
clang -std=c11 -O2 -g3 -Wall -Wextra \
  --target=riscv32-unknown-elf \
  -fuse-ld=lld \
  -fno-stack-protector \
  -ffreestanding \
  -nostdlib \
  -Iinclude \
  -Wl,-Tkernel/arch/riscv32/kernel.ld \
  -Wl,-Map=kernel.map \
  -o kernel.elf \
  $(find kernel lib -name "*.c")
```

生成される主なファイルは次の通りです。

- `kernel.elf`: QEMU に渡すカーネル本体
- `kernel.map`: リンク結果のマップファイル

## 起動方法

```sh
./run.sh
```

`run.sh` は内部で次の処理を行います。

1. `clang --target=riscv32-unknown-elf` と `lld` で `kernel.elf` をビルドします。
2. `qemu-system-riscv32 -machine virt` を起動します。
3. `opensbi-riscv32-generic-fw_dynamic.bin` を BIOS として指定します。
4. `fs.ext4` を virtio-blk デバイスとして接続します。
5. virtio-gpu / virtio-keyboard / virtio-mouse を接続します。
6. SDL ウィンドウで画面を表示します。

起動後は QEMU の SDL ウィンドウと、`-serial stdio` によるターミナル出力を確認してください。

## 起動コマンドの中身

手動で QEMU を起動する場合は、`kernel.elf` をビルドした後に以下を実行します。

```sh
qemu-system-riscv32 \
  -machine virt \
  -bios opensbi-riscv32-generic-fw_dynamic.bin \
  -kernel kernel.elf \
  -serial stdio \
  -drive if=none,format=raw,file=fs.ext4,id=hd0 \
  -device virtio-blk-device,drive=hd0 \
  -device virtio-gpu-device \
  -device virtio-keyboard-device \
  -device virtio-mouse-device \
  -global virtio-mmio.force-legacy=false \
  -display sdl
```

## トラブルシュート

- `qemu-system-riscv32: command not found` が出る場合は、`qemu-system-misc` をインストールしてください。
- `ld.lld` が見つからない場合は、`lld` をインストールしてください。
- SDL 表示で起動できない場合は、`qemu-system-gui` や SDL 関連パッケージが入っているか確認してください。
- GUI のない環境や SSH 先では、`run.sh` の `-display sdl` が失敗することがあります。

## 関連ドキュメント

- `docs/overview.md`: 起動から実行までの概要
- `docs/architecture.md`: ディレクトリ構成
- `docs/GPU-driver.md`: virtio-gpu ドライバの説明
- `docs/debugging.md`: GDB デバッグのメモ
