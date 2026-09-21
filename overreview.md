# 現在の構成レビュー

この文書は、現在のソースツリーを基準に、ファイル配置、公開API分離、主要サブシステム、ビルド状態を整理したものです。

## 現在の全体構成

このリポジトリは RISC-V 32bit 向けの小さなモノリシックカーネル構成になっている。

主なディレクトリ責任は以下。

```text
arch/riscv32/
  RISC-V 32bit 固有の boot、trap entry、context switch、PLIC、linker script。

arch/riscv32/include/arch/
  RISC-V 固有の公開ヘッダ。
  現在は CSR 操作と trap frame を置いている。

drivers/virtio/
  VirtIO block / GPU / input の低レイヤドライバ。

fs/
  ext4 read-only 実装。

kernel/
  カーネル本体。
  process、scheduler、VM、FS窓口、syscall、trap、event、GUI/WM、SBI、kernel main を含む。

kernel/include/
  カーネル共通ヘッダと、カーネル内部サブシステムの公開API。

lib/
  freestanding 環境用の printf / string 実装。

kernel/apps/
  user app 風に書かれているが、現状は create_process() で起動されるカーネル内プロセス群。
```

## 公開API分離の状態

以前は `kernel/include/kernel/kernel.h` に多くの責任が集まっていたが、現在は小さいヘッダへ分割されている。

現在の主なヘッダ責任は以下。

| ヘッダ | 責任 |
| --- | --- |
| `arch/riscv32/include/arch/csr.h` | `READ_CSR`, `WRITE_CSR` |
| `arch/riscv32/include/arch/trap.h` | RISC-V 用 `struct trap_frame` |
| `kernel/include/kernel/trap.h` | `kernel_entry()`, `handle_trap()` |
| `kernel/include/kernel/proc.h` | process構造体、process状態、fork/exec/wait/yield/mmap系 |
| `kernel/include/kernel/vm.h` | SV32 VM API |
| `kernel/include/kernel/page_alloc.h` | page allocator API |
| `kernel/include/kernel/fs.h` | FS窓口 API |
| `kernel/include/kernel/sbi.h` | SBI、console、shutdown |
| `kernel/include/kernel/panic.h` | `PANIC` |
| `kernel/include/kernel/syscall.h` | syscall番号、共有struct、`handle_syscall()` |
| `kernel/include/kernel/waitq.h` | wait queue |
| `kernel/include/kernel/event.h` | kernel event queue |
| `kernel/include/kernel/blk.h` | block driver API |
| `kernel/include/kernel/virtio_gpu.h` | VirtIO GPU API |
| `kernel/include/kernel/virtio_input.h` | VirtIO input API |
| `kernel/include/kernel/wm.h` | window manager API |
| `kernel/include/kernel/ext4.h` | ext4 read-only API |

`kernel/include/kernel/kernel.h` は互換用の umbrella header として残っている。中身は各責任別ヘッダの include と、`kernel_main()` / `boot()` の宣言のみ。

## アーキ依存の分離

CSR操作と trap frame は RISC-V 固有なので、`kernel/include/kernel/` ではなく `arch/riscv32/include/arch/` に置かれている。

```text
arch/riscv32/include/arch/csr.h
arch/riscv32/include/arch/trap.h
```

`kernel/include/kernel/trap.h` は `arch/trap.h` を include し、カーネル側の trap API だけを公開する薄いヘッダになっている。

## ビルド構成

`Makefile` は次の include path を使う。

```sh
-Iarch/riscv32/include
-Ikernel/include
```

ビルド対象は以下。

```sh
find arch/riscv32 drivers fs kernel lib -name "*.c"
```

linker script は以下。

```text
arch/riscv32/kernel.ld
```

`scripts/run.sh` も同じ include path、source list、linker script を使う。

## 起動と初期化

`kernel/main.c` の `kernel_main()` が中心。

現在の初期化順はおおむね以下。

1. BSSを0クリア
2. trap entryを `stvec` に設定
3. `sscratch`, `sie`, `sstatus` を初期化
4. kernel event queueを初期化
5. PLICを初期化
6. VMを初期化
7. idle processを作成
8. 外部割り込みを有効化
9. FSを初期化
10. `user_init_entry()` を process として起動
11. scheduler loopへ入る

## プロセスとスケジューラ

process関連は `kernel/proc/process.c` と `kernel/proc/sched.c` が担当。

`struct process` と公開APIは `kernel/include/kernel/proc.h` に分離済み。

現在の process には以下が含まれる。

- pid / state / priority / budget
- parent pid / exit status
- user/kernel process種別
- kernel stack
- trap frame
- `sepc`
- `satp`
- VMA配列
- fd配列

スケジューラは `yield()` による協調的な切り替えで、RUNNABLEかつbudgetが残っているprocessから選ぶ。

## VMとメモリ

VM実装は `kernel/memory/vm.c`。

公開APIは `kernel/include/kernel/vm.h` に分離済み。

page allocatorは `kernel/memory/page_alloc.c`。

公開APIは `kernel/include/kernel/page_alloc.h` に分離済み。

VMは SV32 前提で、user process向けに page table を構築し、user page の map/query/unmap を提供している。

## FS

FS窓口は `kernel/fs/fs.c`。

公開APIは `kernel/include/kernel/fs.h`。

ext4 read-only 実装は `fs/ext4.c` にある。公開APIは `kernel/include/kernel/ext4.h`。

現状の `fs_init()` では ext4 mount は temporary skip され、RAMFSが主経路になっている。

## デバイス

VirtIO系ドライバは `drivers/virtio/` にある。

- `virtio_blk.c`: block read
- `virtio_gpu.c`: GPU初期化、backbuffer、present、IRQ
- `virtio_input.c`: input初期化、event取得、IRQ

公開APIはそれぞれ以下。

- `kernel/include/kernel/blk.h`
- `kernel/include/kernel/virtio_gpu.h`
- `kernel/include/kernel/virtio_input.h`

PLICは `arch/riscv32/plic.c` にあり、公開APIは `kernel/include/kernel/plic.h`。

## GUI / WM

Window Manager実装は `kernel/gui/wm.c`。

公開APIは `kernel/include/kernel/wm.h`。

GPUとinputを使い、window作成、focus、text/image設定、render、mouse/input event処理などを提供している。

## syscall

syscall実装は `kernel/syscall/syscall.c`。

公開定義は `kernel/include/kernel/syscall.h`。

現在の `kernel/apps` は syscall wrapper ではなく、`user_app.h` で `fs_open`, `wm_create`, `putchar` などのカーネル関数へ直接つなぐ形が多い。

そのため syscall経路は存在するが、起動シェル相当の動線では直接カーネル関数呼び出しが中心。

## 現在の作業ツリー状態

この文書作成直前の `git status --short` は空で、作業ツリーは clean に見えていた。

その後、この `overreview.md` を追加したため、現在はこのファイルが新規差分になる。

## ビルド確認

以下を実行した。

```sh
make build
```

結果:

```text
ビルド成功
```

実際のコンパイルでは以下が使われている。

```sh
clang ... -Iarch/riscv32/include -Ikernel/include \
  -Wl,-Tarch/riscv32/kernel.ld \
  -o kernel.elf \
  $(find arch/riscv32 drivers fs kernel lib -name "*.c")
```

## 残っている課題

1. `kernel/include/kernel/kernel.h` は互換用に残っている。完全に不要にできるかは後で確認できる。
2. syscall ABIとカーネル内部APIはまだ `kernel/include/kernel/syscall.h` に同居している。将来的には `abi/syscall.h` のように分ける余地がある。
3. `kernel/apps` は user app 風だが、現状はカーネル内processとして動くため、本当のuser/kernel境界とはまだ一致していない。
4. ext4実装はあるが、通常FS経路では mount がskipされている。
