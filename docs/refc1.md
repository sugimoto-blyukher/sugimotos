# kernel/include 公開API責任分離の差分メモ

## 目的

`kernel/include/kernel/kernel.h` に集まっていたカーネル内部APIを、責任別の小さいヘッダへ分割した。

今回の方針は、実装ファイルを大きく移動するのではなく、まず公開APIの境界を整理すること。既存コードとの互換性を残すため、`kernel/kernel.h` は削除せず umbrella header として残した。

## 行ったこと

### 1. `kernel.h` の責任を分割

以前の `kernel/include/kernel/kernel.h` には以下が混在していた。

- CSR操作
- panic macro
- trap frame
- SBI/console
- process構造体とprocess API
- page allocator API
- filesystem API
- VM API
- boot/kernel entry宣言

これを次のヘッダへ分割した。

| 追加ファイル | 役割 |
| --- | --- |
| `arch/riscv32/include/arch/csr.h` | `READ_CSR`, `WRITE_CSR` |
| `arch/riscv32/include/arch/trap.h` | RISC-V用 `struct trap_frame` |
| `kernel/include/kernel/trap.h` | `kernel_entry`, `handle_trap` |
| `kernel/include/kernel/sbi.h` | `struct sbiret`, `sbi_call`, `putchar`, `getchar`, `sbi_shutdown` |
| `kernel/include/kernel/page_alloc.h` | `alloc_pages`, `alloc_pages_try`, `free_pages`, refcount操作 |
| `kernel/include/kernel/proc.h` | process状態定数、`struct process`, fork/exec/wait/yield/mmap系 |
| `kernel/include/kernel/fs.h` | `fs_init`, `fs_open`, `fs_read`, `fs_write`, `fs_listdir` など |
| `kernel/include/kernel/vm.h` | `vm_init`, `vm_activate`, user page map/query/unmap |
| `kernel/include/kernel/panic.h` | `PANIC` |

`kernel/include/kernel/kernel.h` は、上記ヘッダをまとめて include する互換ヘッダにした。

その後、`csr.h` と `struct trap_frame` はRISC-V依存の内容だったため、`arch/riscv32/include/arch/` 配下へ移動した。`kernel/include/kernel/trap.h` は、アーキ依存の `arch/trap.h` を include しつつ、カーネル側のtrap entry APIを公開する薄いヘッダとして残した。

### 2. 依存関係を細くした

`syscall.h` と `waitq.h` が `kernel.h` に依存していたため、必要なヘッダだけを見るように変更した。

- `kernel/include/kernel/syscall.h`
  - `kernel/kernel.h` 依存をやめた。
  - `common.h` と `kernel/trap.h` を include する形にした。
- `kernel/include/kernel/waitq.h`
  - `kernel/kernel.h` 依存をやめた。
  - `kernel/lock.h` と `kernel/proc.h` を include する形にした。

実装側も、可能な範囲で `kernel/kernel.h` ではなく責任別ヘッダを直接 include する形へ寄せた。

例:

- `kernel/proc/sched.c`: `proc.h`, `vm.h`
- `kernel/memory/vm.c`: `page_alloc.h`, `proc.h`, `vm.h`
- `kernel/memory/page_alloc.c`: `lock.h`, `page_alloc.h`, `panic.h`
- `kernel/fs/fs.c`: `ext4.h`, `fs.h`, `proc.h`, `syscall.h`
- `kernel/syscall/syscall.c`: `csr.h`, `event.h`, `fs.h`, `proc.h`, `sbi.h`, device headers
- `kernel/trap/trap.c`: `syscall.h`, `csr.h`, `plic.h`, `blk.h`, `proc.h`, `sbi.h`, device headers

現在 `kernel/kernel.h` を直接 include しているのは `arch/riscv32/boot.c` のみ。

### 3. include path とビルド対象を修正

既存の `Makefile` と `scripts/run.sh` は、実際には存在しない `include` ディレクトリを `-Iinclude` で参照していた。

今回、実際のヘッダ配置に合わせて以下へ変更した。

```sh
-Ikernel/include
```

また、`Makefile` のビルド対象が `kernel lib` のみだったため、実装が存在する以下も対象へ入れた。

- `arch/riscv32`
- `drivers`
- `fs`
- `kernel`
- `lib`

これに合わせて linker script の参照も `arch/riscv32/kernel.ld` にした。

### 4. 古い include 参照を修正

`kernel/apps/user_app.h` にあった以下の参照は、現在のディレクトリ構成と合っていなかった。

```c
#include "../../include/common.h"
```

これを include path 経由の形式へ変更した。

```c
#include "common.h"
```

## 差分サマリ

`git diff --stat` の結果:

```text
 Makefile                        |   6 +-
 arch/riscv32/context.c          |   2 +-
 arch/riscv32/trap_entry.c       |   2 +-
 drivers/virtio/virtio_blk.c     |   2 +-
 drivers/virtio/virtio_gpu.c     |   3 +-
 fs/ext4.c                       |   1 -
 kernel/apps/user_app.h          |   6 +-
 kernel/event/event.c            |   6 +-
 kernel/fs/fs.c                  |   3 +-
 kernel/gui/wm.c                 |   2 +-
 kernel/include/kernel/kernel.h  | 190 ++--------------------------------------
 kernel/include/kernel/syscall.h |   3 +-
 kernel/include/kernel/waitq.h   |   2 +-
 kernel/main.c                   |  11 ++-
 kernel/memory/page_alloc.c      |   3 +-
 kernel/memory/vm.c              |   4 +-
 kernel/proc/process.c           |   5 +-
 kernel/proc/sched.c             |   3 +-
 kernel/sbi/sbi.c                |   2 +-
 kernel/syscall/syscall.c        |   4 +
 kernel/trap/trap.c              |   3 +
 scripts/run.sh                  |   8 +-
 22 files changed, 61 insertions(+), 210 deletions(-)
```

追加された新規ヘッダ:

```text
arch/riscv32/include/arch/csr.h
arch/riscv32/include/arch/trap.h
kernel/include/kernel/fs.h
kernel/include/kernel/page_alloc.h
kernel/include/kernel/panic.h
kernel/include/kernel/proc.h
kernel/include/kernel/sbi.h
kernel/include/kernel/trap.h
kernel/include/kernel/vm.h
```

## ファイル別の主な変更

### `kernel/include/kernel/kernel.h`

巨大な定義本体を削除し、責任別ヘッダをまとめる umbrella header に変更。

```c
#include "arch/csr.h"
#include "kernel/fs.h"
#include "kernel/page_alloc.h"
#include "kernel/panic.h"
#include "kernel/proc.h"
#include "kernel/sbi.h"
#include "kernel/trap.h"
#include "kernel/vm.h"
```

`kernel_main()` と `boot()` の宣言は互換のため残している。

現在は `kernel/csr.h` ではなく、RISC-V依存ヘッダの `arch/csr.h` を include する。

### `kernel/include/kernel/syscall.h`

`kernel/kernel.h` 依存を削除し、syscall ABIに必要な `common.h` と `trap.h` だけを見る形に変更。

### `kernel/include/kernel/waitq.h`

`struct process` と `PROC_MAX` が必要なため、`proc.h` を include する形に変更。

### 実装ファイル群

暗黙に `kernel.h` から拾っていた宣言を、用途別のヘッダから拾うように変更。

これにより、各 `.c` の依存が次のように読みやすくなった。

- process関連は `proc.h`
- VM関連は `vm.h`
- page allocator関連は `page_alloc.h`
- SBI/console関連は `sbi.h`
- CSR操作は `csr.h`
- trap frame/entry関連は `trap.h`
- FS関連は `fs.h`

### `Makefile` / `scripts/run.sh`

include path と source list を実配置に合わせて修正。

変更前:

```sh
-Iinclude
find kernel lib -name "*.c"
```

変更後:

```sh
-Ikernel/include
find arch/riscv32 drivers fs kernel lib -name "*.c"
```

## 確認結果

ヘッダ依存を確認するため、通常の `make build` だけでなく一度生成物を消してからフルビルドした。

実行した確認:

```sh
make clean
make build
```

結果:

```text
make build 成功
```

## 残っている課題

`kernel/kernel.h` は互換用に残している。今後さらに整理するなら、次の順で進めるとよい。

1. `arch/riscv32/boot.c` も `kernel/kernel.h` ではなく必要最小限のヘッダへ寄せる。
2. `kernel/kernel.h` を使っていない状態を維持できるか確認する。
3. 必要なら `kernel/kernel.h` を deprecated 扱いにする。
4. syscall ABIをさらに分けるなら、`kernel/include/abi/syscall.h` を作り、ユーザ/カーネル共有定義とカーネル内部関数を分離する。
