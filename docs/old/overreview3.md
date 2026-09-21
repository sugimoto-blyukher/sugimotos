# 現状の実装状況まとめ（overreview3）

この文書は、現行の `kernel/`、`include/kernel/`、`kernel/apps/` の実装を読み取って、現在どこまで動く形になっているかを日本語で整理したものです。以前の `docs/overreview2.md` から進んでいる点もありますが、一部は現在のコードでは無効化・未接続になっています。

## 1. 起動と初期化

- 対象は RISC-V 32bit S-mode。
- `boot` から `kernel_main` に入り、BSS クリア、`stvec`/`sscratch`/割り込み関連 CSR の初期設定を行う。
- 初期化順はおおむね次の通り。
  - カーネルイベントキュー初期化
  - PLIC 初期化
  - SV32 仮想メモリ初期化
  - idle プロセス作成
  - 外部割り込み有効化
  - ファイルシステム初期化
  - `user_init_entry` 起動
- 現在の `user_init_entry` は `create_user_process` ではなく `create_process` で起動されているため、実質的にはカーネルスレッドとして動く。`kernel/apps/user_app.h` でも syscall wrapper ではなく、`putchar`、`fs_open`、`wm_create` などのカーネル関数を直接呼ぶ形に置き換えられている。
- メインループには孤児ゾンビ回収と `user_init_entry` 再 spawn の分岐がある。ただし現在の `has_live_app_proc()` は idle プロセスも live 扱いしうるため、shell 消滅時にこの再 spawn 分岐が期待通り発火するかは要確認。

## 2. トラップ・割り込み・システムコール

- アセンブリの `kernel_entry` がトラップ入口として使われ、C 側の `handle_trap` に渡す。
- `handle_trap` は以下を処理する。
  - supervisor external interrupt
  - U/S-mode `ecall`
  - ユーザページフォルト
  - その他のユーザトラップのログ出力と `proc_exit(128)`
  - カーネルトラップ時のシャットダウン
- 外部割り込みでは PLIC を claim/complete し、virtio-input、virtio-blk、virtio-gpu の IRQ ハンドラを呼ぶ。
- syscall 番号は `include/kernel/syscall.h` に定義済み。実装済みの主な syscall は以下。
  - `putchar`, `getchar`, `yield`, `shutdown`
  - `fork`, `exit`, `wait`, `waitpid`, `exec`
  - `open`, `close`, `read`, `write`, `unlink`, `rename`, `listdir`
  - `mmap`, `munmap`
  - `gpu_init`, `gpu_info`, `gpu_present`
  - `input_init`, `input_next_event`
  - `event_poll`
- `SYS_WMCTL` と `WMCTL_*` の定義は残っているが、現在の syscall dispatcher では `SYS_WMCTL` は常に失敗する。WM は syscall 経由ではなくカーネル内直接呼び出しで使われている。

## 3. プロセス管理

- 固定長プロセステーブル方式。上限は `PROC_MAX=8`。
- プロセス状態は `UNUSED`、`RUNNABLE`、`BLOCKED`、`ZOMBIE`。
- `create_process` はカーネルスレッドを作成する。
- `create_user_process` はユーザプロセス用の SATP、ユーザスタック、trap frame、VMA を構築する実装がある。ただし現在の起動経路では `user_init_entry` に使われていない。
- `fork` はユーザプロセス向けに実装されている。
  - trap frame を複製し、子の戻り値は `a0=0`。
  - FD テーブルを複製する。
  - writable VMA は COW 化される。
- `exec` は entry PC と `argv` を検証し、動的 mmap 領域を落として trap frame を再初期化する。
- `exit`、`wait`、`waitpid` は実装済み。`WNOHANG` に対応する。
- 注意点として、現行の主要 UI/shell はカーネルスレッドとして動いているため、ユーザプロセス機構は実装済みでも主経路では限定的にしか使われていない。

## 4. スケジューラ

- `yield()` ベースの協調スケジューリング。
- runnable なプロセスから、残り budget が大きいものを選ぶ簡易 priority/budget 方式。
- budget が尽きたら runnable プロセスの budget を priority で補充する。
- 切り替え時は次プロセスの `satp` を有効化し、`switch_context` で `ra` と `s0-s11` 系の保存復帰を行う。
- idle は `wfi` と `yield` を繰り返す。
- タイマ割り込みによるプリエンプティブスケジューリングは未実装。

## 5. メモリ管理

- SV32 のページテーブルを使う。
- カーネル側は CLINT、PLIC、UART/virtio MMIO、RAM などを恒等マップする。
- ユーザ SATP 作成時は、カーネルページテーブルをコピーした上で、ユーザ text/rodata/data/bss とユーザスタックを U bit 付きでマップする。
- ページアロケータは以下を持つ。
  - `alloc_pages_try` / `alloc_pages`
  - `free_pages`
  - free run リスト
  - refcount 配列
  - COW 用の `page_inc_ref` / `page_dec_ref`
  - 二重解放や範囲外解放の panic 検出
- `mmap` / `munmap` はユーザ VMA 管理として実装済み。
  - 動的 mmap 範囲は `0x20000000` から `0x3f000000`。
  - `MAP_FIXED` に対応する。
  - ページは demand allocation され、ページフォルト時に割り当てられる。
  - store page fault では COW 解決を試みる。

## 6. ファイルシステム

### RAMFS

- 現在の主ファイルシステムは RAMFS。
- 固定上限は `RAMFS_MAX_FILES=32`、ファイル名 32 byte、データ 1024 byte。
- `README` という初期ファイルを作る。
- `open/create/trunc/append/read/write/unlink/rename/listdir` が実装済み。
- FD 上限はプロセスごとに `FD_MAX=16`。

### ext4

- `kernel/fs/ext4.c` には ext4 読み取り用の実装がある。
  - virtio-blk 初期化
  - superblock 読み取り
  - group descriptor / inode 読み取り
  - extent depth 0/1 の追跡
  - root directory scan
  - file lookup / read / listdir
- ただし `fs_init` では現在 `ext4_mount()` 呼び出しがコメントアウトされ、`ext4 mount skipped (temporary)` として無効化されている。そのため通常の `fs_open` から ext4 ファイルを読む経路は現時点では使われない。

## 7. デバイスドライバ

- SBI:
  - console putchar/getchar
  - shutdown
- PLIC:
  - supervisor external interrupt 用の claim/complete 経路。
- virtio-blk:
  - virtio-mmio デバイス探索
  - queue 初期化
  - 512 byte sector read
  - 複数スロットの request 管理
  - IRQ で waitq を起こす
- virtio-gpu:
  - virtio 1.0 前提の初期化
  - display info 取得
  - 2D resource 作成
  - backing attach
  - set scanout
  - transfer/flush による present
  - 最大 1024x768 に制限した backbuffer
  - `gpu_info` / `gpu_present` syscall からユーザ提供バッファを表示可能
- virtio-input:
  - mouse/tablet/keyboard などの virtio-input デバイスを最大 4 個まで探索
  - デバイス名から優先度をつけて初期化
  - イベントをラウンドロビンで取得
  - IRQ 時にカーネルイベントキューへ input event を流す

## 8. イベントキュー

- `kernel/event/event.c` に固定長リングバッファのカーネルイベントキューがある。
- 容量は 128。
- push 時に満杯なら古いイベントを落として前進する。
- input IRQ と gpu IRQ が `KEVENT_TYPE_INPUT` / `KEVENT_TYPE_GPU_IRQ` として入る。
- `SYS_EVENT_POLL` はこのキューから `sys_event` へ変換して返す。
- 現在の `user_init_entry` は syscall ではなく `kevent_pop` を直接呼んで入力を読む経路を持つ。

## 9. ウィンドウマネージャ

- カーネル内 WM として実装されている。
- 主な機能は以下。
  - 初期化時に virtio-gpu を初期化
  - window create / move / resize / focus / close
  - text 設定
  - image 設定
  - window event ring
  - focus in/out、pointer move、wheel、key、overflow などのイベント種別定義
  - dirty rect 管理
  - GPU backbuffer への簡易描画と present
- `include/kernel/wm.h` には `raise`、`set_visible`、`tile`、`dump_state`、capture、drag などの API 宣言もあるが、現行 `wm.c` では一部だけが実装・利用されている。古い実装や予定機能と思われるコードはコメントアウトされている箇所が多い。
- syscall 経由の WM 操作は現在未接続で、shell/GUI は直接 `wm_*` 関数を呼ぶ。

## 10. shell / アプリ層

- `kernel/apps/user_init.c` が現在の shell 兼簡易 GUI 起動点。
- 起動時に以下を行う。
  - `wm_init`
  - `wm_input_init`
  - shell history 初期化
  - ファイル一覧用の簡易 DB 初期化
  - GUI 自動起動が有効なら terminal window を作成
- 現在の shell コマンドとして確認できるもの。
  - `help`
  - `ls`
  - `cat`
  - `gui`
  - `shutdown`
- `help` 表示には `touch`、`write`、`rm`、`img`、`fm`、`exit` も含まれているが、現行の command dispatch では処理されていない。
- `user_fm.c` と `user_img.c` は存在するため、ファイルマネージャや画像表示の部品はあるが、現在の shell からは直接起動されていない。
- キーボード入力は virtio-input の key event を ASCII に変換し、足りない場合は SBI の `getchar` にフォールバックする。

## 11. ビルド・実行

- `run.sh` は以下を行う。
  - `clang --target=riscv32-unknown-elf` と lld で `kernel.elf` をビルド
  - `kernel/arch/riscv32/kernel.ld` を linker script として使用
  - `kernel.map` を生成
  - QEMU `qemu-system-riscv32 -machine virt` で OpenSBI 経由起動
  - `fs.ext4` を virtio-blk として接続
  - virtio-gpu、virtio-keyboard、virtio-mouse を接続
  - `virtio-mmio.force-legacy=false`
  - SDL display を使用

## 12. 現在の主な制約・未完了点

- shell/GUI は現在カーネルスレッドとして起動され、ユーザプロセス・syscall 分離は主経路では使われていない。
- `SYS_WMCTL` は定義済みだが dispatcher では未実装。
- ext4 読み取り実装はあるが、`fs_init` でマウントが無効化されている。
- `help` に表示されるコマンドと実際の shell dispatch にずれがある。
- スケジューリングは協調型で、タイマ割り込みによるプリエンプションはない。
- プロセス数、FD 数、RAMFS ファイル数、RAMFS ファイルサイズ、WM ウィンドウ数はいずれも固定上限。
- ユーザ VM は VMA、demand allocation、COW まであるが、汎用的な ELF loader やファイル backed mmap はない。
- virtio-gpu/input/blk は実装されているが、環境は QEMU virtio-mmio 前提。
- 一部のヘッダ宣言と実装の対応、コメントアウトされた WM 機能、アプリ部品の接続状態に未整理な箇所が残っている。
