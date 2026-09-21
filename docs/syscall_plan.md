# trap・syscall・実処理の責務

```text
U-mode ecall
  → arch/riscv32/trap/entry.c     レジスタ・sepc・sstatusを保存
  → arch/riscv32/trap/trap.c      例外・割り込みの種別を判定
  → arch/riscv32/trap/syscall.c   a7とa0〜a5を番号・引数へ変換
  → kernel/syscall/syscall.c     syscall番号で振り分け
  → kernel/syscall/sys_*.c       引数検証・ユーザーバッファ変換
  → fs/・mm/・kernel/process/    各サブシステムの実処理
  ← arch/riscv32/trap/           a0・復帰PCを設定し、sret
```

## arch側

`struct trap_frame`とCSR操作はarch側が扱う。ecallのPC更新、SUMの設定、
戻り値をa0に書き込む処理もここに置く。trap frameにはsepcとsstatusを含め、
ユーザーメモリへのコピー中にページフォルトが発生しても、元の復帰状態を保持する。

`arch/riscv32/process.c`はプロセスの初期レジスタとfork時のレジスタ複製を扱う。
`trap/resume.c`が新しいユーザープロセスの実行を開始する。

## syscall側

公開APIは次のとおり。RISC-Vのtrap frameやCSRを引数に含めない。

```c
struct syscall_result syscall_dispatch(uint32_t number, const uint32_t args[6]);
```

通常の結果は`value`で返す。exec成功時だけ`context_replaced`を立て、arch側に
プロセスの新しい実行状態を復元させる。execのargcを通常の戻り値で上書きしない。

- `sys_fs.c`: パスのコピーとバッファの検証を行い、`fs_*`へ渡す。
- `sys_proc.c`: `proc_fork/exec/exit/waitpid`とschedulerへ接続する。
- `sys_vm.c`: `proc_mmap/munmap`へ接続する。
- `sys_device.c`: console、GPU、eventのAPIへ接続する。
- `syscall_internal.h`: dispatcherとhandler間の内部宣言。

番号や共有データ形式は`kernel/include/uapi/`に置く。今回の分割では、作業中の
`uapi/syscall.h`の番号を維持する。旧一体型syscall.hの番号との互換性は持たない。
`sys_event`は既存event queueとアプリが使うtype/a/b/c/d/seq形式にそろえる。
typeは`EVENT_INPUT`または`EVENT_GPU_IRQ`で、キー・マウスの詳細はa/b/cに入る。
`SYS_WMCTL`は引数ABIが未定義のため、引き続き-1を返す。
GPU情報のframebufferはユーザーマッピング未実装のため0を返す。

## 実処理

- `fs/fs.c`, `fs/ext4.c`: ファイル、FD、ファイルシステム処理。
- `mm/page_alloc.c`, `mm/vm.c`: 物理ページ、ページテーブル。
- `mm/process_vm.c`: VMA、mmap、munmap、COW、ページフォルト、アドレス検証。
- `mm/usercopy.c`: ユーザー領域とのコピー。呼び出し中のSUM管理はarch側が担当する。
- `kernel/process/`: プロセス生成・終了・fork・exec・wait・スケジューリング。

実処理はsyscall番号を解釈しない。ページフォルト原因もarch側でread/write/executeの
アクセス種別へ変換してからmmへ渡す。

## 検証

```sh
make build
python3 scripts/test_syscalls.py
```

テストは一時ディレクトリに専用kernelを作り、QEMUのU-modeからecallを実行する。
通常のkernel.elfとディスクイメージは変更しない。未知番号、レジスタ保存、yield、
無効ポインタ、ファイル操作、mmap先へのコピー中のページフォルト、fork、exec、
waitpidを確認する。Python 3、clang/lld、qemu-system-riscv32が必要。
通常のシェルはS-modeでカーネルAPIを直接呼ぶため、このテストでsyscall経路を別途確認する。
