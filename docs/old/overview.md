# プログラム動作解説（概要）

このディレクトリは、RISC-V（Sモード）向けの最小カーネルと補助コードで構成されています。
SBI（OpenSBI）を介してコンソール出力を行い、トラップ入口と簡易スケジューリングの骨組みを備えています。
詳細は `overreview2.md` を参照してください。

## 起動から実行までの流れ（概略）

1. **ブート入口**（`boot`）
   - `.text.boot` セクションに配置された `boot` が最初に実行されます。
   - `sp` を `__stack_top` に設定し、`kernel_main` にジャンプします。

2. **カーネル初期化**（`kernel_main`）
   - `__bss` を 0 クリアします。
   - `stvec` に `kernel_entry` を設定し、トラップ入口を登録します。
   - プロセス（`idle_proc`, `proc_a`, `proc_b`）を作成し、`yield()` でスケジューリングを開始します。

3. **トラップ入口**（`kernel_entry`）
   - 例外発生時に CPU が `stvec` へ遷移して `kernel_entry` を実行します。
   - 全汎用レジスタをスタックに退避し、`trap_frame` として `handle_trap` に渡します。
   - `handle_trap` から戻ると、レジスタを復元し `sret` で復帰します。

## 主な構成（詳細は別紙）

- 詳細: `overreview2.md`
