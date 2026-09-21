# 現在実装されている機能

この文書は、現時点のソースコードをベースに、実装済みの機能を整理したものです。設計予定や将来仕様ではなく、実コードに存在する機能と、その接続状況を中心にまとめます。

## 対象環境

このOSはRISC-V 32bit向けのモノリシックカーネルです。

`run.sh` では以下の構成でビルド・起動します。

- `clang --target=riscv32-unknown-elf` と `ld.lld` で `kernel.elf` を生成する。
- QEMU `virt` machineで起動する。
- OpenSBIをBIOSとして使う。
- `virtio-blk`, `virtio-gpu`, `virtio-keyboard`, `virtio-mouse` を接続する。
- 表示は `-display sdl` を使う。

## ブートとカーネル初期化

`kernel/main.c` の `kernel_main()` がカーネル初期化の中心です。

実装済みの初期化処理は以下です。

1. BSS領域を0クリアする。
2. 起動バナーを表示する。
3. `stvec` にtrap entryを設定する。
4. `sscratch`, `sie`, `sstatus` を初期化する。
5. kernel event queueを初期化する。
6. PLICを初期化する。
7. SV32ページングを初期化する。
8. idle processを作成する。
9. 外部割り込みを有効化する。
10. ファイルシステムを初期化する。
11. シェル相当の `user_init_entry()` をprocessとして起動する。
12. scheduler loopへ入る。

アプリプロセスが存在しなくなった場合は、最後のuser trap情報を表示しつつ `user_init_entry()` を再生成する処理があります。

## 割り込み・トラップ

`kernel/trap/trap.c` にtrap処理があります。

実装済みの処理は以下です。

- U-mode/S-modeからの `ecall` をsyscallとして処理する。
- Supervisor external interruptをPLIC経由で処理する。
- VirtIO input, block, GPUのIRQ handlerを呼ぶ。
- U-modeのinstruction/load/store page faultをVM側へ渡す。
- ユーザープロセスの復旧不能trapではtrap contextをdumpし、プロセスをexitさせる。
- カーネル側の想定外trapではcontextをdumpしてshutdownする。

現状の外部割り込み処理は、IRQ番号が64以下であれば各VirtIO handlerを順に呼ぶ方式です。各handler側で自分のデバイス状態を確認するため、IRQごとの厳密な分岐はまだありません。

## PLIC

`kernel/arch/riscv32/plic.c` にPLIC制御があります。

実装済みの機能は以下です。

- IRQ 1から64までのpriorityを1に設定する。
- S-mode context向けに下位64 sourceを有効化する。
- thresholdを0に設定する。
- `plic_claim()` / `plic_complete()` を提供する。

単一hart前提の実装です。

## スケジューラとプロセス

プロセス管理は `kernel/proc/process.c`、スケジューラは `kernel/proc/sched.c` にあります。

### プロセス構造

最大プロセス数は `PROC_MAX 8` です。

各processは以下を持ちます。

- pid
- state
- priority
- budget
- parent pid
- exit status
- user/kernel process種別
- kernel stack
- trap frame
- `sepc`
- `satp`
- VMA配列
- fd配列

process stateは以下です。

- `PROC_UNUSED`
- `PROC_RUNNABLE`
- `PROC_BLOCKED`
- `PROC_ZOMBIE`

### scheduler

`yield()` は、RUNNABLEかつbudgetが残っているprocessから、budgetが最大のものを選びます。

- 現在processがeligibleならbudgetを1減らす。
- 次のRUNNABLE processを探す。
- 見つからなければ全RUNNABLE processのbudgetをpriorityで補充する。
- `vm_activate(next->satp)` でアドレス空間を切り替える。
- `switch_context()` でcontext switchする。

単純なpriority/budget方式の協調的スケジューリングです。

### process作成

実装済みの作成経路は2種類あります。

- `create_process(pc)`: カーネルアドレス空間で動くprocessを作る。
- `create_user_process(entry_pc)`: U-mode向けprocessを作る。

ただし、現在の起動シェルは `create_process((uint32_t)user_init_entry)` で作られており、実質的にはカーネル内processとして動いています。

### fork / exec / wait / exit

syscall経由で使えるプロセス機能として、以下が実装されています。

- `fork`
- `exec`
- `exit`
- `wait`
- `waitpid`
- `yield`

`fork` はユーザープロセス専用で、書き込み可能VMAをcopy-on-write化します。子processの `a0` は0、親processには子pidが返ります。

`exec` はentry pcとargvを受け取り、dynamic mappingを落としてVMAを初期化し、trap frameを作り直します。

`wait` / `waitpid` は子processのZOMBIEを回収します。`WNOHANG` も定義されています。

## 仮想メモリ

仮想メモリは `kernel/memory/vm.c` と `kernel/proc/process.c` のVMA処理で構成されています。

### SV32ページング

実装済みの機能は以下です。

- SV32用のroot page table作成
- kernel用identity mapping
- `satp` 切り替え
- 4MiB mappingと4KiB mapping
- user pageのmap/unmap/query
- user用 `satp` の構築

kernel mappingには、PLIC、MMIO、RAM領域などがidentity mappingされています。

### User VMA

ユーザープロセスは最大 `PROC_VMA_MAX 16` 個のVMAを持ちます。

VMA protectionは以下です。

- `VMA_PROT_R`
- `VMA_PROT_W`
- `VMA_PROT_X`

user process作成時には、user text/rodata/data/bss/stackに対応するVMAが登録されます。

### page fault

U-modeのpage faultは `proc_handle_user_page_fault()` が処理します。

実装済みの動作は以下です。

- fault addressをpage alignする。
- 対応するVMAを探す。
- fault種別に対して必要なpermissionを確認する。
- 未mapなら物理ページを確保してmapする。
- store faultでCOW対象なら新しいページを確保してコピーし、書き込み可能mapへ差し替える。

### mmap / munmap

ユーザープロセス向けに `proc_mmap()` / `proc_munmap()` が実装されています。

`mmap` は `USER_MMAP_BASE 0x20000000` から `USER_MMAP_END 0x3f000000` の範囲を使います。

実装済みの機能は以下です。

- page alignedなVMA確保
- hint address対応
- `MAP_FIXED` 対応
- protection bit検証
- `munmap` によるVMA削除、縮小、分割
- map済みページのunmapとfree

ファイルbacked mmapではなく、anonymous mapping相当です。

## 物理ページアロケータ

`kernel/memory/page_alloc.c` にページ単位の物理メモリアロケータがあります。

実装済みの機能は以下です。

- `__free_ram` から `__free_ram_end` までを管理対象にする。
- page単位で確保する。
- free listをrun単位で管理する。
- free runの隣接結合を行う。
- page refcountを持つ。
- `page_inc_ref()` / `page_dec_ref()` を提供する。
- spinlockで保護する。
- double freeや範囲外freeはpanicする。

COW forkはこのrefcountを使っています。

## 同期プリミティブ

### spinlock

`include/kernel/lock.h` にspinlockがあります。

- `spin_lock`
- `spin_unlock`
- `spin_lock_irqsave`
- `spin_unlock_irqrestore`

`irqsave` 版は `sstatus.SIE` を落としてからlockし、unlock時に元の `sstatus` を復元します。

### wait queue

`kernel/proc/waitq.c` にwait queueがあります。

実装済みの機能は以下です。

- wait queue初期化
- current processをwaiterに登録してBLOCKEDにする
- `yield()` で他processへ譲る
- `wake_all` でwaiterをRUNNABLEに戻す

VirtIO block/GPUなどの待ち合わせに使われています。

## Kernel event queue

`kernel/event/event.c` にカーネルイベントキューがあります。

実装済みの機能は以下です。

- 固定長128個のリングバッファ
- `kevent_push()`
- `kevent_pop()`
- sequence番号付与
- overflow時は最古イベントを捨てて前進する
- spinlock + irqsaveで保護する

イベント種別は現在以下です。

- `KEVENT_TYPE_INPUT`
- `KEVENT_TYPE_GPU_IRQ`

VirtIO input IRQは入力イベントをこのqueueへ積みます。GPU IRQもイベントとして積まれます。

## syscall

`include/kernel/syscall.h` と `kernel/syscall/syscall.c` にsyscallが実装されています。

現在dispatcherで処理されているsyscallは以下です。

- `SYS_PUTCHAR`
- `SYS_YIELD`
- `SYS_FORK`
- `SYS_EXIT`
- `SYS_WAIT`
- `SYS_EXEC`
- `SYS_WAITPID`
- `SYS_GETCHAR`
- `SYS_OPEN`
- `SYS_CLOSE`
- `SYS_READ`
- `SYS_WRITE`
- `SYS_UNLINK`
- `SYS_LISTDIR`
- `SYS_SHUTDOWN`
- `SYS_RENAME`
- `SYS_MMAP`
- `SYS_MUNMAP`
- `SYS_GPU_INIT`
- `SYS_GPU_INFO`
- `SYS_GPU_PRESENT`
- `SYS_INPUT_INIT`
- `SYS_INPUT_NEXT_EVENT`
- `SYS_EVENT_POLL`

ユーザープロセスからのポインタ引数には、VMAに基づくreadable/writable検証が入っています。syscall中は必要に応じて `SSTATUS_SUM` を立て、kernelからuser memoryを参照できるようにしています。

`SYS_WMCTL` は定義されていますが、dispatcherでは常に失敗します。現時点ではWM操作syscallは未接続です。

## ファイルシステム

ファイルシステムは `kernel/fs/fs.c` が窓口です。

### RAMFS

現在実際に有効なFSはRAMFSです。

制限は以下です。

- 最大ファイル数: 32
- ファイル名最大長: 31文字程度
- 1ファイル最大サイズ: 1024 bytes
- 階層ディレクトリなし
- pathは先頭 `/` を許容するが、1階層の名前に正規化される

実装済み操作は以下です。

- `open`
- `close`
- `read`
- `write`
- `unlink`
- `rename`
- `listdir`

`O_CREAT`, `O_TRUNC`, `O_APPEND`, `O_RDONLY`, `O_WRONLY`, `O_RDWR` が定義されています。

起動時には `README` というRAMFSファイルが作られ、内容は `ramfs ready\n` です。

### ext4

`kernel/fs/ext4.c` にはext4 read-only実装があります。

実装済みの要素は以下です。

- VirtIO block経由のblock読み込み
- superblock読み込み
- group descriptor読み込み
- inode読み込み
- extent形式のfile data読み込み
- root directory scan
- root直下ファイルのlookup
- root直下ファイルのread
- root直下ファイル一覧取得

ただし、現時点の `fs_init()` では `ext4 mount skipped (temporary)` として `ext4_ready = false` にしており、通常のFS経路からはext4は使われません。ext4実装は存在しますが、現在の起動動線では無効です。

## VirtIO block

`kernel/drivers/virtio_blk.c` にVirtIO block readドライバがあります。

実装済みの機能は以下です。

- MMIO領域からVirtIO block deviceを探索する。
- queue 0を初期化する。
- 最大8件のblock request slotを持つ。
- 512 byte sector単位でreadする。
- used ringを処理する。
- IRQでwait queueをwakeする。
- request slotが空くまでwait queueで待つ。

書き込みは実装されておらず、read専用です。

## VirtIO GPU

`kernel/drivers/virtio_gpu.c` にVirtIO GPUドライバがあります。

実装済みの機能は以下です。

- MMIO領域からVirtIO GPU deviceを探索する。
- VirtIO 1.0 featureを確認する。
- queue 0を初期化する。
- `GET_DISPLAY_INFO` で表示情報を取得する。
- `RESOURCE_CREATE_2D` で2D resourceを作成する。
- backbuffer用物理ページを確保する。
- `RESOURCE_ATTACH_BACKING` でresourceにbackbufferを接続する。
- `SET_SCANOUT` でresourceをscanoutへ割り当てる。
- `TRANSFER_TO_HOST_2D` と `RESOURCE_FLUSH` で表示更新する。
- IRQ時にwait queueをwakeし、kernel eventへGPU IRQを積む。

解像度はdisplay modeから取得し、最大 `1024x768` に制限されます。取得できない場合は `640x480` にフォールバックします。

`SYS_GPU_PRESENT` ではユーザー側bufferをGPU backbufferへコピーし、画面にpresentできます。

## VirtIO input

`kernel/drivers/virtio_input.c` にVirtIO inputドライバがあります。

実装済みの機能は以下です。

- MMIO領域からVirtIO input deviceを探索する。
- device nameを読み、mouse/tablet/keyboardを優先順付きで並べる。
- 最大4デバイスを初期化する。
- queue 0に入力イベント受信用descriptorを投入する。
- round-robinでイベントを取得する。
- IRQ時に入力イベントをkernel event queueへ積む。

対応しているイベント定義は、key、relative movement、absolute movementの基本値です。

シェル側では `u_getchar()` が `KEVENT_TYPE_INPUT` を読み、英数字・一部記号・Enter・Backspace・Tab・上下キー・Shiftを処理して文字入力へ変換します。

## GUI / Window Manager

GUIの現状は `docs/GUI_current.md` に詳しくまとめています。

ここでは実装済み機能だけを要約します。

- カーネル内Window Managerがある。
- 最大8個のウィンドウを固定配列で管理する。
- ウィンドウ作成、移動、リサイズ、フォーカス、破棄がある。
- Z順を管理する。
- ウィンドウごとにテキストと画像バッファを持てる。
- GPU backbufferへ背景、ウィンドウ矩形、画像、白い矩形カーソルを描画できる。
- dirty rectを1個の統合矩形として管理する。
- ウィンドウごとのイベントリングを持つ。
- フォーカス変更時に `FOCUS_IN` / `FOCUS_OUT` を積む。
- マウス相対移動と左クリックによるフォーカス変更の処理がある。

ただし、現在のシェル/GUIコードはWMをsyscall経由ではなく直接関数呼び出しで使っています。また、GPU描画パスではテキスト描画、タイトルバー、タスクバー、ウィンドウ装飾はまだありません。

## シェルと内蔵コマンド

`kernel/apps/user_init.c` にシェル相当の処理があります。

現在のシェルはS-modeのprocessとして動いており、`user_app.h` でsyscall wrapperではなくカーネル関数を直接呼ぶ形になっています。

実装済みコマンドは以下です。

- `help`: コマンド一覧表示
- `ls`: RAMFSのファイル一覧表示
- `cat <path>`: ファイル読み込み表示
- `touch <path>`: 空ファイル作成
- `write <path> <text...>`: ファイルへ文字列を書き込み
- `rm <path>`: ファイル削除
- `gui`: terminalウィンドウ作成/描画
- `img <path>`: ImageViewerウィンドウ作成
- `fm`: FileManagerウィンドウ作成
- `shutdown`: SBI shutdown

シェルには簡易履歴機能のデータ構造と関数もありますが、現状の入力ループでは上下キーの履歴移動処理には接続されていません。

## 簡易アプリ

### FileManager

`kernel/apps/user_fm.c` にあります。

実装済みの動作は以下です。

- 既にactiveなら既存ウィンドウへfocusする。
- 未起動なら `FileManager` ウィンドウを作る。
- 固定テキストを設定して描画する。
- shell側のRAMFS操作に追随する簡易file db追加/削除/rename関数を持つ。

実ファイル一覧UI、選択、プレビュー、rename UIはまだありません。

### ImageViewer

`kernel/apps/user_img.c` にあります。

実装済みの動作は以下です。

- `ImageViewer` ウィンドウを作る。
- path文字列をウィンドウテキストへ設定する。
- 描画する。

画像ファイルの読み込み、デコード、ピクセル表示はまだありません。

## libc相当の補助関数

`lib/string.c` と `lib/printf.c` に最低限のCライブラリ相当実装があります。

実装されている代表的な関数は以下です。

- `memset`
- `memcpy`
- `memcmp`
- `strlen`
- `strncpy`
- `printf`

`printf` はSBI console出力を使うカーネルデバッグ出力の基盤です。

## SBI

`kernel/sbi/sbi.c` にはSBI呼び出しがあります。

実装済みの用途は以下です。

- console putchar
- console getchar
- shutdown

## 現在「実装はあるが通常動線では限定的」なもの

以下はコードとして存在しますが、現状の通常起動経路では限定的、または未接続です。

- `create_user_process`: 実装済みだが、現在のシェルは `create_process` で起動している。
- user syscall群: 実装済みだが、現在のシェルは多くを直接カーネル関数呼び出しで使っている。
- `SYS_WMCTL`: 定義済みだがdispatcherでは失敗する。
- ext4: read-only実装はあるが、`fs_init()` でmountをskipしている。
- WMのテキスト描画: `wm_set_text()` はあるが、GPU描画には反映されない。
- WMのマウスpoll: 実装はあるが、現在のシェルメインループで継続pollされていない。
- shell history: 関数はあるが、現在の入力処理では履歴移動に接続されていない。

## まとめ

現在のコードベースでは、以下の土台が実装済みです。

- RISC-V S-modeカーネル起動
- trap/syscall/interrupt処理
- PLIC制御
- 簡易processとscheduler
- user process向けVM、VMA、page fault、COW fork、mmap
- 物理ページアロケータとrefcount
- RAMFS
- read-only ext4実装
- VirtIO block read
- VirtIO GPU表示
- VirtIO input
- kernel event queue
- 簡易Window Manager
- S-modeシェルと内蔵コマンド

一方で、現在の実行環境はまだ「本格的なユーザー空間OS」ではなく、シェルやGUIはカーネル関数直接呼び出しに寄っています。ユーザープロセス/syscall経由への整理、ext4 mount有効化、GUIのテキスト描画とWM syscall接続が、次の大きな接続ポイントです。
