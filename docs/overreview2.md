# カーネル実装機能まとめ（現行コード準拠）

この文書は、`kernel/` と `include/kernel/` の現行実装から、OSカーネルに**実装済み**の機能を整理したものです。

## 1. 起動・CPU制御

- RISC-V 32bit S-mode 向けブート (`boot`)。
- `kernel_main` で以下を実施:
  - BSS初期化
  - `stvec`/`sscratch` 設定
  - 割り込み無効化 (`sie=0`, `sstatus.SIE=0`)
  - 仮想メモリ初期化 (`vm_init`)
  - ファイルシステム初期化 (`fs_init`)
  - ウィンドウマネージャ初期化 (`wm_init`)
  - 入力初期化 (`wm_input_init`)
- SBI経由のコンソール出力/入力/シャットダウン。

## 2. トラップ・システムコール

- アセンブリのトラップ入口 (`kernel_entry`) で全GPR退避/復帰。
- `handle_trap`:
  - U/S-mode `ecall` をシステムコールへディスパッチ
  - それ以外のユーザトラップは `proc_exit(128)`
  - カーネルトラップは `PANIC`
- システムコール実装:
  - `putchar`, `getchar`, `yield`, `shutdown`
  - `fork`, `exit`, `wait`, `waitpid`, `exec`
  - `open`, `close`, `read`, `write`, `unlink`, `rename`, `listdir`
  - `wmctl`（ウィンドウ作成/フォーカス/描画/イベント取得/画像設定など）
- ユーザポインタ検証:
  - 文字列境界チェック
  - 書き込み可能領域チェック
  - `exec(argv)` 形式チェック

## 3. プロセス管理

- 固定長プロセステーブル (`PROC_MAX=8`)。
- カーネルスレッド作成 (`create_process`)。
- ユーザプロセス作成 (`create_user_process`):
  - ユーザスタック2ページ確保
  - ユーザ用 `satp` 構築
  - `sret` 復帰経路でユーザ空間開始
- `fork` 実装:
  - 親のユーザスタック複製
  - trap frame複製（子の返り値 `a0=0`）
  - FDテーブル複製
- `exec` 実装:
  - エントリPCと `argv` 検証
  - trap frame再初期化
- `exit`/`wait`/`waitpid` 実装:
  - ゾンビ化
  - 親のブロック解除
  - `WNOHANG` 対応
- カーネル側で孤児ゾンビの回収処理あり。
- ユーザプロセスが全滅した場合、`user_init` を自動再spawn。

## 4. スケジューラ

- 協調型ラウンドロビン (`yield`)。
- `PROC_RUNNABLE` のユーザプロセスを巡回選択。
- 実行対象切替時に:
  - `satp` 切替
  - `sscratch` 更新
  - `switch_context` で `s0-s11`/`ra` 保存復元
- idleプロセスは `wfi + yield` で待機。

## 5. メモリ管理（SV32）

- ページアロケータ:
  - `alloc_pages`/`free_pages`
  - フリーラン管理（連結リスト）
  - 連結可能領域のcoalesce
  - 末尾解放時のバンプ巻き戻し
- 仮想メモリ:
  - SV32ページテーブル生成
  - カーネル恒等マッピング（CLINT/PLIC/UART/VIRTIO/RAM）
  - ユーザ `satp` 構築
  - ユーザ text/rodata/data/bss + ユーザstackをUビット付きでマップ
  - `sfence.vma` 実行

## 6. ファイルシステム

### RAMFS
- 固定長inodeベースの簡易RAMFS:
  - `open/create/trunc/append`
  - `read/write`
  - `unlink/rename`
  - `listdir`

### ext4（読み取り系）
- `virtio-blk` 上のext4をマウント。
- スーパーブロック/グループ記述子/ inode読み取り。
- extent（depth 0/1）追跡でファイル読み取り。
- ルートディレクトリ走査・検索・一覧。
- `fs_open(O_RDONLY)` で ext4 ファイルをFDとして扱える実装。

## 7. デバイスドライバ

### SBI
- `sbi_call`
- 文字出力 (`putchar`)
- 文字入力 (`getchar` + キュー)
- 電源断 (`sbi_shutdown`)

### virtio-blk
- MMIOデバイス探索
- virtqueue初期化
- 512Bセクタ読み取り (`blk_read`)

### virtio-gpu
- デバイス初期化（virtio 1.0）
- scanout情報取得
- 2Dリソース作成/attach/set_scanout
- バックバッファ転送とflush (`virtio_gpu_present`)

### virtio-input
- 入力デバイス探索（mouse/tablet/keyboard優先）
- 複数デバイスのイベント受信
- ラウンドロビンでイベント取り出し

## 8. ウィンドウマネージャ（カーネル内）

- 複数ウィンドウ管理（作成/移動/リサイズ/前面化/フォーカス/可視切替/クローズ）。
- テキスト描画と画像描画（ウィンドウ内バッファ保持）。
- イベントキュー:
  - フォーカスIN/OUT
  - ポインタ移動/ボタン
  - ホイール
  - キー入力
  - オーバーフロー通知
- capture機能、カーソル移動、ドラッグ移動/リサイズ。
- `virtio-gpu` が使えない場合のテキスト描画経路を保持。

## 9. ユーザ空間起動基盤

- カーネルは `user_init_entry` を最初のユーザプロセスとして起動。
- ユーザ側は syscall ABI 経由で:
  - プロセス制御
  - ファイル操作
  - WM操作
  を利用可能。

## 10. 現状の制約（コードから確認できる範囲）

- プロセス数・FD数は固定上限（`PROC_MAX=8`, `FD_MAX=16`）。
- スケジューリングは協調型（プリエンプティブではない）。
- ext4 は実質読み取り中心（`fs_write` はRAMFSのみ）。
- MMU保護はあるが、汎用的なユーザ空間VM（任意マップ/ページフォルトハンドリング）は未実装。
- カーネル内WM実装であり、表示・入力制御の多くはカーネル空間側に存在。
