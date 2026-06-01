# syscall 分割設計

## Summary

`syscall.c` を dispatcher に絞り、実処理は `kernel/syscall/sys_*.c` に機能別分割する。内部 handler 宣言は `kernel/syscall/syscall_internal.h` に集約し、外部公開ヘッダとは分離する。

## Key Changes

- `kernel/include/kernel/syscall.h`
  - `handle_syscall(struct trap_frame *, uint32_t)` だけを公開する。
  - syscall番号やユーザー共有structは置かない。

- `kernel/include/uapi/*.h`
  - syscall番号、`O_*`, `MAP_*`, event/gpu/wm 共有structを置く。
  - ユーザー側とカーネル側で共有してよい ABI 定義だけに限定する。

- `kernel/syscall/syscall_internal.h`
  - `sys_*` handler の宣言だけを置く。
  - `kernel/syscall/syscall.c` と `sys_*.c` からだけ include する。
  - `fs_open()` や `proc_exit()` などのカーネル本体APIはここへ移さない。

- `kernel/syscall/syscall.c`
  - `f->a7` の syscall番号を見て `sys_*` を呼ぶ dispatcher にする。
  - 引数の取り出しと戻り値設定だけを担当する。
  - user pointer validation や具体処理は原則 `sys_*.c` 側へ寄せる。

- `kernel/syscall/sys_*.c`
  - 機能別に分ける。
  - `sys_fs.c`: open/close/read/write/unlink/rename/listdir
  - `sys_proc.c`: yield/exit/fork/exec/wait/waitpid
  - `sys_vm.c`: mmap/munmap
  - `sys_device.c`: gpu/input/event/wmctl/console

## Fixes Included

- 壊れている `kernel/syscall/sys_proc.c` を完成させる。
- `kernel/include/uapi/event.h` の `struct sys_event` 末尾セミコロン漏れを直す。
- `syscall.c` の switch が `uapi/syscall.h` の全 syscall 番号を扱うようにする。
- 未実装 syscall は `-1` を返す stub にするか、既存実装へ接続する。

## Test Plan

- `make clean`
- `make build`
- `rg "SYS_" kernel/include/uapi kernel/syscall` で、定義済み syscall が dispatcher に存在することを確認。
- `rg "sys_.*\\(" kernel/syscall` で、`syscall_internal.h` の宣言と実装の対応を確認。
- 可能なら QEMU 起動で shell 初期化、簡単な file read/write、yield/exit 経路を確認。

## Assumptions

- 分割粒度は「機能別」を採用する。
- `kernel/syscall/syscall_internal.h` は公開APIではなく syscall subsystem 内部用とする。
- `kernel/include/uapi/` はユーザー/カーネル共有ABI置き場として維持する。
- `kernel/include/kernel/*.h` はカーネル内部サブシステムの公開API置き場として維持する。
