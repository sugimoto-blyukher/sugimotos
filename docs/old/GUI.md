# GUI サブシステム仕様書（Monolithic Kernel） v0.1

> 対象：自作OS（モノリシックカーネル）にGUI環境（複数ウィンドウ + 入力 + 合成）を実装するための最小仕様。  
> 方針：**「描画」より「調停（合成・フォーカス・配送）」が本体**。ここを曖昧にすると、動いても使えないOSが出来る。

---

## 0. 目的と範囲

### 0.1 目的
- ユーザ空間プロセス（GUIクライアント）が
  - ウィンドウを作成し、
  - 共有メモリ上のサーフェスに描画し、
  - `PRESENT` で合成を要求し、
  - 入力イベントを受信できる
- カーネルが
  - 入力を正規化し、
  - フォーカス・ヒットテストに基づき配送し、
  - 複数サーフェスを合成して表示に出力する

### 0.2 非目的（v0.1ではやらない）
- 3D / GPUアクセラレーション（将来拡張点としては残す）
- TrueType/OTF など高度なテキストレンダリング
- マルチディスプレイ
- アクセシビリティ
- 完全なセキュリティ（ただし最低限の権限制御は入れる）

---

## 1. 全体アーキテクチャ（カーネル内）

カーネル内部に以下のコンポーネントを持つ。

### 1.1 Display Subsystem
- 物理表示への出力（virtio-gpu / UEFI GOP / VBE / 生フレームバッファ 等）
- scanout バッファ管理（最終フレーム）
- 可能ならダブルバッファ相当（ティアリング・ちらつき抑制）

### 1.2 Input Subsystem
- キーボード・ポインタ等のデバイスドライバ
- 正規化（キーコード、修飾キー、座標、ボタン、ホイール）
- 割り込みでやるのは **enqueueまで**（重い処理は禁止）

### 1.3 GUI Core（Window Manager + Compositor）
- Window / Surface / Z-order / Focus の管理
- 合成（compositing）
- 入力イベント配送（dispatch）
- カーソル描画

### 1.4 User API（/dev/gui）
- fd を介してウィンドウ作成・状態変更・イベント受信
- サーフェスは `mmap` で共有、描画はクライアントが行う
- `PRESENT(damage)` で合成要求

---

## 2. 用語
- **Window**: 画面上の矩形。位置・サイズ・Z順・可視などを持つ。
- **Surface**: Window の内容（ピクセルバッファ）。クライアントが描く。
- **Compositor**: 複数Surfaceを合成して最終フレームを作る。
- **Damage**: 再描画が必要な矩形集合（汚れ領域）。

---

## 3. 機能要件

### 3.1 表示（必須）
- 基準ピクセルフォーマット：`XRGB8888` もしくは `ARGB8888`（32bpp）
- 最終フレームはカーネル所有の **scanoutバッファ** に生成し表示へ出す
- ちらつき防止：
  - 内部ダブルバッファ（ページフリップ可能なら採用、無理ならmemcpyでも可）

### 3.2 ウィンドウ（必須）
- 作成 / 破棄
- 位置 / サイズ変更
- 可視 / 不可視
- 前面化（Z順操作）
- タイトル文字列保持（装飾描画は任意）

### 3.3 サーフェス（必須）
- クライアントごとにサーフェスを `mmap` 可能
- 標準：**2枚（front/back）** を持つ（ダブルバッファ）
- クライアントは back に描き、`PRESENT` で front に昇格（参照切替 or swap）
- `PRESENT` は damage 矩形（0個以上）を受け取る

> v0.1でdamage最適化を実装できなくても、APIには必ず残す。未来の自分の時間を買う。

### 3.4 入力・イベント（必須）
- キー：down/up、修飾キー状態（Shift/Ctrl/Alt等）
- ポインタ：move（x,y + dx,dy）、ボタン down/up、ホイール
- フォーカス：
  - クリックでフォーカス（focusable windowのみ）
  - `FOCUS_IN/FOCUS_OUT` を通知
- 各クライアントにイベントキュー（リング）を持たせ、`read()` で取得可能
- `poll/select` で readable になること

### 3.5 カーソル（必須）
- グローバルカーソルをカーネルが描画
- v0.1は固定形状でOK（将来 `SET_CURSOR` 拡張）

---

## 4. 非機能要件

### 4.1 安定性
- 不正引数・不正ID・権限違反でカーネルが落ちない（境界チェック徹底）
- GUIコアの重処理は割り込みコンテキストで実行しない

### 4.2 性能
- v0.1は全面合成でも許容
- 将来のために damage ベース合成へ移行できる構造にする

### 4.3 観測性
- GUI状態のダンプ（/proc/gui など）を提供

---

## 5. カーネル内部オブジェクト設計

### 5.1 Window
- `u32 id`
- `pid_t owner_pid`
- `rect {u32 x,y,w,h}`
- `i32 z_index`（もしくはリスト順序）
- `bool visible`
- `bool focusable`
- `u32 surface_id`
- `char title[64]`

### 5.2 Surface
- `u32 id`
- `pid_t owner_pid`
- `u32 format`（XRGB8888/ARGB8888）
- `u32 width, height, stride`
- `buffer[2]`（front/back）
- `u32 front_index`（0/1）
- `damage_queue`（直近の damage 矩形）

### 5.3 GUI Session（プロセス単位）
- `pid_t pid`
- `event_ring`
- `owned_windows[]`
- `permissions`（後述）

---

## 6. 合成パイプライン仕様

### 6.1 基本フロー
1. クライアントが back buffer に描画
2. `PRESENT(window_id, damage_rects[])`
3. GUI Core が対応 surface の front/back を切替（参照切替 or swap）
4. damage を global damage にマージ
5. compositor thread が global damage を処理し scanout に反映
6. 表示更新（vsyncが取れるなら同期、取れないならタイマで周期更新）

### 6.2 v0.1ルール
- damage が実装負荷なら **全画面更新** でも可
- ただし API には damage を残し、内部実装は段階的に最適化する

---

## 7. イベント配送仕様

### 7.1 キーイベント
- フォーカスウィンドウの `owner_pid` にのみ配送

### 7.2 ポインタイベント
- (x,y) がヒットする visible window のうち **最前面** に配送
- ボタン押下中のドラッグを成立させるために **capture** をサポート（推奨）
  - 押下したウィンドウに配送を固定（releaseまで）

### 7.3 フォーカス遷移
- クリックでフォーカス（対象がfocusableの場合）
- フォーカスが変わったら
  - 旧：`FOCUS_OUT`
  - 新：`FOCUS_IN`
  を送る

---

## 8. 同期と実行コンテキスト（重要）

### 8.1 ロック戦略（v0.1）
- 最初は粗い `gui_global_lock` でOK（正しく動く方が先）
- 将来：`window_lock` / `surface_lock` / `session_lock` に分割

### 8.2 禁止事項
- 割り込みコンテキストで合成しない
  - 割り込みではイベント enqueue まで
  - 合成は compositor thread で実行

---

## 9. ユーザAPI（/dev/gui）

### 9.1 open/close
- `open("/dev/gui", O_RDWR)` で GUI session を生成し fd を返す
- `close(fd)` で session 破棄
  - 所有 window/surface は破棄する（リーク防止）

### 9.2 read（イベント受信）
- `read(fd, buf, n)` で `gui_event` 配列を取得
- イベント無し：
  - デフォルトはブロック
  - `O_NONBLOCK` なら `-EAGAIN`

### 9.3 poll/select
- イベントが到着したら readable

### 9.4 ioctl 一覧（v0.1）
- `GUI_IOCTL_GET_DISPLAY_INFO`
  - 画面サイズ、format、refresh(不明なら0)
- `GUI_IOCTL_CREATE_WINDOW`（x,y,w,h, flags）-> window_id
- `GUI_IOCTL_DESTROY_WINDOW`（window_id）
- `GUI_IOCTL_SET_WINDOW_RECT`（window_id, x,y,w,h）
- `GUI_IOCTL_SET_WINDOW_VISIBLE`（window_id, bool）
- `GUI_IOCTL_RAISE_WINDOW`（window_id）
- `GUI_IOCTL_SET_TITLE`（window_id, title[64]）
- `GUI_IOCTL_CREATE_SURFACE`（w,h,format, flags）-> surface_id
- `GUI_IOCTL_ATTACH_SURFACE`（window_id, surface_id）
- `GUI_IOCTL_PRESENT`（window_id, damage_rects[]）
- `GUI_IOCTL_SET_CAPTURE`（window_id, on/off）
- `GUI_IOCTL_SET_FOCUS`（window_id）※実験用（基本は内部で決める）

---

## 10. mmap（サーフェス共有メモリ）

### 10.1 方式（v0.1推奨：A案）
- **A案（単純）**
  - `GUI_IOCTL_MAP_SURFACE(surface_id, which_buffer)` で offset を得る
  - `mmap(fd, offset, size, PROT_WRITE|PROT_READ, MAP_SHARED)` でマップ
- **B案（後回し）**
  - buffer handle / dmabuf 的な抽象（将来）

### 10.2 権限
- 自分の `surface_id` のみ mmap 可能
- 他プロセスの surface を mmap しようとしたら `-EPERM`

---

## 11. ABI（固定サイズ構造体）

### 11.1 rect
- `u32 x, y, w, h`

### 11.2 event（例）
- 共通ヘッダ：
  - `u32 type`
  - `u32 window_id`
  - `u64 timestamp_ms`（単調増加）
- 種類：
  - `GUI_EV_KEY`（keycode, pressed, modifiers）
  - `GUI_EV_POINTER_MOVE`（x,y, dx,dy）
  - `GUI_EV_POINTER_BUTTON`（button, pressed, x,y）
  - `GUI_EV_WHEEL`（delta）
  - `GUI_EV_FOCUS`（in/out）
  - `GUI_EV_EXPOSE`（damage rect）
  - `GUI_EV_CLOSE_REQUEST`（window_id）
  - `GUI_EV_OVERFLOW`（キュー溢れ通知）

> timestamp は必須。イベント詰まりや入力遅延の解析が一気に楽になる。

---

## 12. 最低限の権限・分離（v0.1）

- **グローバルキーイベント購読は禁止**
  - キーはフォーカス所有者にしか流さない（キーロガー封じ）
- **ウィンドウ操作権限**
  - `DESTROY/SET_RECT/RAISE` は owner のみ
- **特権WM（将来拡張点）**
  - permissions に「WM権限」フラグを用意しておく（今は使わなくてもよい）

---

## 13. エラーハンドリング規約
- 無効ID/無効引数：`-EINVAL`
- 権限なし：`-EPERM`
- メモリ不足：`-ENOMEM`
- イベントキュー満杯：
  - 通常イベントはドロップ
  - `GUI_EV_OVERFLOW` を **1回だけ** 通知（通知スパム禁止）

---

## 14. デバッグ・観測性

### 14.1 /proc/gui（例）
- window 一覧：id, owner, rect, z, visible, surface_id
- surface 一覧：id, owner, size, mapped
- focus 状態、capture 状態
- compositor 統計：fps, 合成時間, damage面積（取れる範囲で）

---

## 15. 実装マイルストーン（壊れにくい順）

- **M0**: フレームバッファに塗れる（fill_rect）
- **M1**: `/dev/gui` open/read + イベントリング（中身ダミーでも）
- **M2**: マウスカーソルを動かす（合成の練習台）
- **M3**: window 1枚 + surface mmap + PRESENT（全面合成でOK）
- **M4**: window 複数 + Z順 + ヒットテスト
- **M5**: フォーカス + キー配送
- **M6**: damage 合成（部分更新）
- **M7**: capture + ドラッグ移動（ここで“GUIとして使える”になる）

---

## 16. 将来拡張フック（v0.1に仕込む）
- Surface format を列挙型で保持（RGB565等の追加余地）
- PRESENT API に damage を含める（今使わなくても）
- permissions に WM 権限ビット（将来のユーザ空間WMへ移行可能）

---

## 17. 注意（モノリシックGUIの現実）
- これは速く作れるが、GUIバグ＝カーネルクラッシュになりがち。
- だから v0.1 の勝ち筋は：
  - カーネルは「資源管理 + 合成 + 配送」に寄せる
  - クライアント描画は共有メモリ + present に逃がす
  - 入力は「キュー」「フォーカス」「capture」を最優先で固める

---