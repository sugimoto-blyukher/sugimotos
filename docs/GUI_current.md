# 現在のGUI実装状況

この文書は、`docs/GUI.md` の将来仕様ではなく、現時点のソースコードから読めるGUI実装状況をまとめたものです。

## 概要

現在のGUIは、`/dev/gui` やユーザー空間のGUIサーバーではなく、カーネル内にある簡易Window ManagerをS-modeで動くシェル相当コードから直接呼び出す構成です。

主な実装箇所は以下です。

- Window Manager本体: `kernel/gui/wm.c`
- WM公開ヘッダ: `include/kernel/wm.h`
- VirtIO GPUドライバ: `kernel/drivers/virtio_gpu.c`
- VirtIO Inputドライバ: `kernel/drivers/virtio_input.c`
- シェル/GUI呼び出し側: `kernel/apps/user_init.c`, `kernel/apps/user_app.h`
- 簡易アプリ: `kernel/apps/user_fm.c`, `kernel/apps/user_img.c`

## 実行モデル

`kernel_main()` は `create_process((uint32_t)user_init_entry)` でシェル相当の処理を起動します。

`user_init_entry()` は起動時に次を行います。

1. `wm_init()` でWindow ManagerとGPUを初期化する。
2. `wm_input_init()` でVirtIO inputを初期化する。
3. `USER_INIT_AUTOSTART_GUI` が有効なため、`terminal` ウィンドウを作成する。
4. 作成したターミナルウィンドウへフォーカスを当てる。
5. `wm_render()` で初回描画する。

重要なのは、現在の `kernel/apps/user_app.h` では `u_wm_create` などがsyscall wrapperではなく、`wm_create` などのカーネル関数へ直接マクロ展開されている点です。

```c
#define u_wm_create wm_create
#define u_wm_set_text wm_set_text
#define u_wm_focus wm_focus
#define u_wm_close wm_close
#define u_wm_set_image wm_set_image
#define u_wm_poll_mouse wm_poll_mouse_input
#define u_wm_poll_event wm_poll_event
#define u_wm_render wm_render
```

そのため、現時点のGUIは「ユーザープロセスがsyscall経由でWMへ依頼する」構成ではありません。

## Window Manager

`kernel/gui/wm.c` では、最大8個のウィンドウを固定配列で管理しています。

```c
#define WM_MAX_WINDOWS 8
static struct wm_window wm_windows[WM_MAX_WINDOWS];
```

各ウィンドウは次の情報を持ちます。

- 使用中フラグ
- ウィンドウID
- 表示/非表示
- フォーカス可能フラグ
- 位置とサイズ
- Z順
- タイトル文字列
- 本文テキスト
- 画像バッファ
- イベントリング

実装済みの主な操作は以下です。

- `wm_create`: ウィンドウ作成
- `wm_move`: 移動
- `wm_resize`: リサイズ
- `wm_focus`: フォーカスと前面化
- `wm_close`: 破棄
- `wm_set_text`: テキスト保存
- `wm_set_image`: 画像バッファ設定
- `wm_render`: 描画
- `wm_poll_event`: ウィンドウイベント取得
- `wm_cursor_move`: カーソル移動

一方、`include/kernel/wm.h` に宣言されているものの、`kernel/gui/wm.c` 側に実装が見当たらない関数もあります。

- `wm_raise`
- `wm_set_visible`
- `wm_get_rect`
- `wm_list`
- `wm_tile`
- `wm_dump_state`
- `wm_drag_begin_from_cursor`
- `wm_drag_end`
- `wm_set_capture`
- `wm_get_focus`
- `wm_get_capture`
- `wm_set_desktop_id`

ヘッダ上のAPIと実装にはまだ差分があります。

## 描画

GPUが使える場合、`wm_render()` は `wm_render_gpu()` を呼びます。

現在のGPU描画はかなり最小です。

1. VirtIO GPUのbackbufferを取得する。
2. デスクトップ背景色を塗る。
3. visibleなウィンドウをZ順で並べる。
4. 各ウィンドウの矩形を塗る。
5. ウィンドウに画像があれば単純blitする。
6. 8x8の白い矩形カーソルを描く。
7. `virtio_gpu_present()` で画面へ反映する。

現在の `wm_render_gpu()` はタイトルバー、枠線、本文テキスト、タスクバーの実描画を行っていません。`wm_set_text()` は `struct wm_window.text` に文字列を保存しますが、GPU描画パスではその文字列を描画していません。

また、ダーティ矩形は `wm_dirty_x0/y0/x1/y1` の1矩形として管理されています。描画時にはclipとして使われますが、背景と全ウィンドウを走査するため、最適化はまだ限定的です。

## VirtIO GPU

`kernel/drivers/virtio_gpu.c` はVirtIO GPUの初期化と画面反映を担当します。

実装されている処理は以下です。

- MMIO領域からVirtIO GPUデバイスを探索する。
- VirtIO 1.0 featureを確認する。
- queue 0を初期化する。
- `GET_DISPLAY_INFO` で表示モードを取得する。
- `RESOURCE_CREATE_2D` で2D resourceを作る。
- backbuffer用ページを確保する。
- `RESOURCE_ATTACH_BACKING` でbackbufferをresourceへ接続する。
- `SET_SCANOUT` でscanoutへresourceを割り当てる。
- `TRANSFER_TO_HOST_2D` と `RESOURCE_FLUSH` で表示更新する。

解像度は取得できたdisplay modeを使いますが、上限として `1024x768` に制限されています。display modeが取れない場合は `640x480` にフォールバックします。

`virtio_gpu_present()` は現状、dirty rectではなくresource全体を転送・flushします。

## 入力

`kernel/drivers/virtio_input.c` はVirtIO inputデバイスを初期化し、IRQで入力イベントをkernel event queueへ積みます。

キーボード入力は主に `kernel/apps/user_init.c` の `u_getchar()` で処理されています。

- `KEVENT_TYPE_INPUT` を `kevent_pop()` で取得する。
- `VI_EV_KEY` のみを見る。
- Shift状態を管理する。
- 英数字と一部記号をASCIIへ変換する。
- 上下キーはESC sequenceとしてqueueへ積む。

WM側にも `wm_poll_mouse_input()` があります。

- `VI_EV_REL` の `VI_REL_X/Y` でカーソルを動かす。
- 左クリック時にカーソル下の最前面ウィンドウへフォーカスする。
- 入力があれば全画面dirtyにする。

ただし、現在の `user_init_entry()` のメインループでは `u_wm_poll_mouse()` が定期的に呼ばれていません。そのため、通常のシェル動線ではWM側のマウス処理は継続的には回っていないように見えます。

## イベント

WMにはウィンドウごとのイベントリングがあります。

```c
#define WM_EVENT_RING_SIZE 256
struct wm_event ev_ring[WM_EVENT_RING_SIZE];
```

実装されているイベント種別は `include/kernel/wm.h` にあります。

- `WM_EV_FOCUS_IN`
- `WM_EV_FOCUS_OUT`
- `WM_EV_POINTER_MOVE`
- `WM_EV_POINTER_BUTTON`
- `WM_EV_OVERFLOW`
- `WM_EV_WHEEL`
- `WM_EV_KEY`

現在の実コードで明確に積まれているのは、主にフォーカス変更時の `FOCUS_IN` / `FOCUS_OUT` です。`wm_queue_event()` にはpointer moveやwheelの圧縮処理がありますが、マウス入力処理からpointer eventを配送する処理はまだ実装されていません。

## アプリケーション

### ターミナル

起動時に `terminal` ウィンドウが作成されます。シェル入力や出力の一部は `g_term_view` に蓄積され、`wm_set_text()` でウィンドウへ反映されます。

ただし、現在のGPU描画パスはテキストを描画しないため、実画面上でターミナル本文が見える状態にはまだなっていません。

### FileManager

`kernel/apps/user_fm.c` の `fm_run()` は、`FileManager` ウィンドウを作成し、固定テキストを設定して描画します。

現在は実ファイル一覧UI、選択、プレビュー、リネームなどのGUI操作は未実装です。

### ImageViewer

`kernel/apps/user_img.c` の `img_open_path()` は、`ImageViewer` ウィンドウを作成し、指定されたpath文字列を設定して描画します。

現在は画像ファイルの読み込み、デコード、`wm_set_image()` による画像表示までは実装されていません。

## syscallとの関係

`include/kernel/syscall.h` には `SYS_WMCTL` と `WMCTL_*` が定義されています。

しかし、`kernel/syscall/syscall.c` のdispatcherでは `SYS_WMCTL` は常に `syscall_fail(f)` になります。

つまり、現時点では通常のユーザー空間からsyscall経由でWindow Managerを操作する経路は未接続です。

GPUとinputについては以下のsyscallが存在します。

- `SYS_GPU_INIT`
- `SYS_GPU_INFO`
- `SYS_GPU_PRESENT`
- `SYS_INPUT_INIT`
- `SYS_INPUT_NEXT_EVENT`
- `SYS_EVENT_POLL`

ただし、現在のシェル/GUIコードは多くの操作を直接カーネル関数呼び出しで行っています。

## `docs/GUI.md` との差分

`docs/GUI.md` は、複数ウィンドウ、surface、mmap、PRESENT、イベント配送、`/dev/gui` などを持つ将来仕様に近い内容です。

現在の実装では、以下はまだ未実装または未接続です。

- `/dev/gui`
- GUI session
- ユーザーごとのsurface管理
- front/back surfaceのdouble buffering
- `mmap` によるsurface共有
- `PRESENT(window_id, damage)` API
- `poll/select` 対応のGUIイベント読み出し
- 入力イベントの本格的なhit-test配送
- pointer capture
- キーボードイベントのフォーカスウィンドウ配送
- テキストレンダリング
- タイトルバーや閉じるボタンなどのウィンドウ装飾
- 実用的なFileManager/ImageViewer UI

## 現在できていること

- VirtIO GPUを初期化し、backbufferを画面へ反映できる。
- カーネル内で最大8個の簡易ウィンドウを管理できる。
- ウィンドウの作成、移動、リサイズ、フォーカス、破棄の基礎がある。
- Z順に従ってウィンドウ矩形を描画できる。
- 画像ピクセルバッファをウィンドウへblitする仕組みがある。
- 起動時にターミナル用ウィンドウを作成する。
- `fm` / `img` コマンドで簡易ウィンドウを作る入口がある。
- VirtIO inputからキーボードイベントを受けてシェル入力へ変換できる。
- WM内にウィンドウ別イベントリングの土台がある。

## 現在の主な不足点

- GPU描画でテキストが描かれていない。
- `wm_set_text()` が見た目に反映されない。
- WMのマウス処理がメインループで継続的にpollされていない。
- `SYS_WMCTL` が未実装。
- `include/kernel/wm.h` の宣言と `kernel/gui/wm.c` の実装に差分がある。
- `/dev/gui` 方式のユーザー空間GUI APIはまだ無い。
- surface/mmap/PRESENTの設計は仕様書にあるが、現コードには無い。
- input eventの配送はフォーカスイベント中心で、pointer/keyの本格配送は未完成。
- タイトルバー、タスクバー、文字描画、ウィンドウ装飾は未実装。
- FileManagerとImageViewerはウィンドウ作成の入口だけで、実用UIや画像表示は未完成。

## 短期的な次の実装候補

現状からGUIとして見える成果を出すなら、優先度は以下が高いです。

1. `wm_render_gpu()` に最小ビットマップフォント描画を追加し、`wm_set_text()` の内容を表示する。
2. ウィンドウ枠、タイトルバー、アクティブ表示を描く。
3. `user_init_entry()` のループで `u_wm_poll_mouse()` と `u_wm_render()` を適切な頻度で呼ぶ。
4. `wm.h` に宣言済みで未実装の関数を、実装するか宣言から外す。
5. `SYS_WMCTL` を最小実装して、直接呼び出しからsyscall経由へ戻す。
6. `img` コマンドで実際に画像を読み、`wm_set_image()` へ渡す。
7. その後、`docs/GUI.md` のsurface/mmap/PRESENT設計へ段階的に近づける。
