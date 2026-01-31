# プログラム動作解説（概要）

このディレクトリは、RISC-V（Sモード）向けの最小カーネルと補助コードで構成されています。
SBI（OpenSBI）を介してコンソール出力を行い、トラップ処理の骨組みを備えています。

## 起動からトラップまでの流れ

1. **ブート入口**（`boot`）
   - `.text.boot` セクションに配置された `boot` が最初に実行されます。
   - `sp` を `__stack_top` に設定し、`kernel_main` にジャンプします。

2. **カーネル初期化**（`kernel_main`）
   - `__bss` を 0 クリアします。
   - `stvec` に `kernel_entry` を設定し、トラップ入口を登録します。
   - `unimp` 命令を実行して意図的に例外を発生させ、トラップ経路を確認します。

3. **トラップ入口**（`kernel_entry`）
   - 例外発生時に CPU が `stvec` へ遷移して `kernel_entry` を実行します。
   - 全汎用レジスタをスタックに退避し、`trap_frame` として `handle_trap` に渡します。
   - `handle_trap` から戻ると、レジスタを復元し `sret` で復帰します。

4. **トラップ処理**（`handle_trap`）
   - `scause`, `stval`, `sepc` を読み出し、現状は未処理トラップとして `PANIC` します。

## 主要ファイルと役割

### `kernel.c`
- **ページ確保**: `alloc_pages` が `__free_ram` から物理メモリをページ単位で確保し、ゼロ初期化します。
- **SBI 呼び出し**: `sbi_call` が `ecall` を発行して SBI 機能を呼び出します。
- **文字出力**: `putchar` は SBI で 1 文字出力します。
- **トラップ入口**: `kernel_entry` はレジスタ退避・復帰と `handle_trap` 呼び出しを行います。
- **起動処理**: `kernel_main` が BSS クリアとトラップ設定を行い、例外を発生させます。

### `common.c`
- **基本関数**: `memcpy`, `memset`, `strcmp` を最小実装。
- **簡易 `printf`**: `%s`, `%d`, `%x`, `%%` のみに対応し、`putchar` で出力します。

### `kernel.h`
- **`sbiret`**: SBI 呼び出しの戻り値構造体。
- **`trap_frame`**: トラップ入口で保存するレジスタ一式。
- **CSR アクセス**: `READ_CSR`, `WRITE_CSR` マクロ。
- **`PANIC`**: エラー出力して無限ループ。

### `common.h`
- **型定義**: `uint32_t`, `size_t` などの基本型。
- **ユーティリティ**: `align_up`, `offsetof` など。
- **可変長引数**: `va_list` 系のビルトイン定義。
- **宣言**: `memset`, `memcpy`, `printf` などの宣言。

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

## `printf` の仕様と制限

- 対応書式: `%s`, `%d`, `%x`, `%%`
- `%d`: 符号付き 10 進整数
- `%x`: 32bit 固定 8 桁の 16 進表示
- 非対応: 幅指定、`%u`, `%p`, `%c`, 64bit 値など

---

必要なら `kernel.ld`（リンカスクリプト）や `run.sh`（実行手順）の解説も追記できます。

## 追加メモ

### `kernel.ld` と `run.sh` の解説メモ

- `kernel.ld` はカーネルのセクション配置とシンボル定義（`__bss`, `__bss_end`, `__stack_top`, `__free_ram`, `__free_ram_end` など）を司ります。  
  これらは `kernel.c` の BSS クリアやページ確保の境界に使われます。
- `run.sh` は QEMU などのエミュレータ起動や、OpenSBI の FW とカーネル ELF の組み合わせ実行を自動化するスクリプトであることが多いです。  
  実際の引数やロード先アドレスは `run.sh` を確認して合わせる必要があります。

### `scause` ごとのトラップ処理の分岐案メモ

典型的には `scause` を見て例外/割り込みごとに分岐します。例:

```
if (scause == 8) {         // Environment call from U-mode
    // システムコール処理
} else if (scause == 9) {  // Environment call from S-mode
    // SBI 呼び出しの処理など
} else if (scause == 2) {  // Illegal instruction
    // ログ出力して停止 or 修復
} else if (scause & (1u<<31)) {
    // 割り込み系: タイマ割り込みなど
} else {
    PANIC(...);
}
```

- `scause` の上位ビットで「割り込み/例外」を区別できます。  
- `stval` は不正命令やページフォールト時の補助情報として使います。  
- `sepc` を更新すれば、復帰先の PC を制御できます（例: ecall の次命令へ進める等）。

### `printf` 拡張の設計メモ

最小実装に `%u`, `%p`, `%c` を追加する場合のメモ:

- `%u`: 符号なし 10 進表示。`unsigned` を使い、`%d` と同じ除算ロジックで出力。
- `%p`: ポインタ表示。`uintptr_t` 相当（ここでは `uint32_t`）を 16 進固定桁で出力し、先頭に `0x` を付ける。
- `%c`: 単一文字。`int` を受けて 1 文字だけ `putchar`。
- 64bit 対応が必要なら `uint64_t` 用の桁出しを追加し、`%llx` などの書式拡張を検討。
