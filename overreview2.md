# プログラム動作解説（統合版）

このリポジトリは、RISC-V（Sモード）向けの最小カーネルです。SBI（OpenSBI）経由で文字出力を行い、
トラップ入口・コンテキストスイッチ・簡易スケジューリングの骨組みを備えています。
`docs/overview.md` の内容も取り込み、現在のコードに合わせて整理しています。

## 起動からプロセス実行までの流れ

1. **ブート入口**（`boot`）
   - `.text.boot` に配置された `boot` が最初に実行されます。
   - スタックを `__stack_top` に設定し、`kernel_main` へジャンプします。

2. **カーネル初期化**（`kernel_main`）
   - `__bss` 領域を 0 クリアします。
   - `stvec` に `kernel_entry` を設定し、トラップ入口を登録します。
   - プロセス管理用の構造体を作成します。
     - `idle_proc`（PID=0）を作成し、`current_proc` に設定。
     - `proc_a` と `proc_b` を作成。
   - 最初のスケジューリングとして `yield()` を呼び出します。
   - その後は `PANIC("switch to idle process")` で停止するため、
     期待通りに切り替わらない場合はここで止まります。

3. **プロセス作成**（`create_process`）
   - `procs[]` から空きスロットを探し、初期スタックを準備します。
   - 保存レジスタ群（`s0`〜`s11`）を 0 で埋め、`ra` に開始アドレス（`pc`）をセット。
   - `pid`, `state`, `sp` を初期化し、実行可能状態にします。

4. **スケジューリング**（`yield`）
   - `current_proc` の次から順に実行可能プロセスを探します。
   - 実行対象が変わる場合は、`sscratch` に次プロセスのカーネルスタック先頭を設定し、
     `switch_context` でコンテキストスイッチします。

5. **コンテキストスイッチ**（`switch_context`）
   - 現プロセスの保存レジスタ（`s0`〜`s11`, `ra`）をスタックに退避。
   - `prev_sp` と `next_sp` を入れ替えて、次プロセスのレジスタを復元。
   - `ret` で次プロセスの実行へ移行します。

6. **プロセス本体**（`proc_a_entry`, `proc_b_entry`）
   - 起動時に `printf` でメッセージを出力。
   - 無限ループで `A` または `B` を出力し、`yield()` と `delay()` を繰り返します。

## トラップ処理の流れ

1. **トラップ入口**（`kernel_entry`）
   - `sscratch` からカーネルスタックを取り出し、汎用レジスタを退避。
   - `trap_frame` へのポインタを `handle_trap` に渡します。
   - 復帰時はレジスタを復元し、`sret` で例外元へ戻ります。

2. **トラップ処理**（`handle_trap`）
   - `scause`, `stval`, `sepc` を読み出します。
   - 現在は例外原因の分岐処理は行っておらず、`sepc` を再設定して即復帰するだけです。

## トラップ動作の概略図

```
例外発生
  ↓
stvec = kernel_entry
  ↓
kernel_entry:
  sscratch ← 旧sp
  sp ← sp - 31*4
  レジスタ退避
  a0 ← sp (trap_frame*)
  call handle_trap(a0)
  レジスタ復帰
  sret
```

## SBI（OpenSBI）呼び出しと出力

- `sbi_call` は `ecall` を発行して SBI を呼び出します。
- `putchar` は SBI で 1 文字出力を行います。
- `printf` は最小機能のフォーマッタ（`%s`, `%d`, `%x`, `%%` のみ）です。

## メモリ管理（簡易）

- `alloc_pages(n)` は `__free_ram` から連続したページを確保し、0 クリアします。
- `__free_ram` / `__free_ram_end` は `kernel.ld` で定義されています。

## 主要ファイルと役割

- `kernel.c`
  - ブート、トラップ入口、スケジューリング、プロセス生成などの中核。
- `kernel.h`
  - `trap_frame` 定義、CSR 操作マクロ、`PANIC` マクロ、プロセス構造体。
- `common.c` / `common.h`
  - 最小限の標準関数（`memset`, `memcpy`, `strcmp`）と簡易 `printf`。
- `kernel.ld`
  - セクション配置と BSS・スタック・フリーRAM領域のシンボル定義。
- `run.sh`
  - clang でビルドし、QEMU（riscv32）で起動。

## 実行時の見え方（概略）

- 起動後、`proc_a` と `proc_b` が交互に実行され、コンソールに `A` と `B` が出力されます。
- 出力は `yield()` と `delay()` により切り替わります（タイマ割り込みは未使用）。

## 補足・注意点

- `kernel_entry` の保存/復元は `sscratch` に設定した「現在のプロセスのカーネルスタック先頭」を前提にしています。
  `yield()` 内で `sscratch` を更新してからコンテキストスイッチするため、この順序が崩れると復帰時に破綻します。
- `yield()` 内の `__asm__` は `sscratch` に「次に実行するプロセスのスタック先頭」を設定する意図です。
  例外発生時に `kernel_entry` が正しいスタックへ退避できるようにするための準備です。
  なお、現状の文字列連結は `csrw sscratch` の重複を含んでおり、意図どおりの命令列になっているかは注意が必要です。
- `yield()` は協調的スケジューリング（明示的に呼ぶ方式）であり、タイマ割り込みは未使用です。
  CPU を占有したままの処理があると他のプロセスは実行されません。
- `yield()` の候補探索は `current_proc->pid` を基準に巡回し、`PROC_RUNNABLE` の最初のプロセスを選びます。
  `idle_proc` は PID=0 として特別扱いで残し、他がいない場合の待機先になります。
- トラップ原因（`scause`）ごとの分岐処理は未実装です。
- `kernel_main` は `yield()` 後に `PANIC` を呼ぶため、想定通りに切り替わらない場合は停止します。

## `printf` の仕様と制限

- 対応書式: `%s`, `%d`, `%x`, `%%`
- `%d`: 符号付き 10 進整数
- `%x`: 32bit 固定 8 桁の 16 進表示
- 非対応: 幅指定、`%u`, `%p`, `%c`, 64bit 値など

## `kernel.ld` と `run.sh` の解説メモ

- `kernel.ld` はカーネルのセクション配置とシンボル定義（`__bss`, `__bss_end`, `__stack_top`, `__free_ram`, `__free_ram_end`）を司ります。
  これらは `kernel.c` の BSS クリアやページ確保の境界に使われます。
- `run.sh` は clang でビルドし、QEMU（riscv32）で起動するスクリプトです。
  実行には `qemu-system-riscv32` と `clang` が必要です。

## 実行手順と出力例

```
./run.sh
```

出力例（概略）:

```
starting process A
starting process B
ABABABABAB...
```

`set -xue` のため、`run.sh` 実行時にはビルド/起動コマンドの展開も表示されます。
