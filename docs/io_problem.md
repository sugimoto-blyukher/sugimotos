# I/Oまわりの問題点

この文書は、現在のソースコードをベースに、I/Oまわりの問題点を整理したものです。

対象は主に以下です。

- syscall経由のI/O
- RAMFS / ext4
- VirtIO block
- VirtIO GPU
- VirtIO input
- kernel event queue
- シェル側の入力処理

## 重要度の高い問題

### 1. `SYS_WRITE` がユーザーバッファを検証していない

`kernel/syscall/syscall.c` では、`SYS_READ` は出力先バッファに対して `proc_user_writable_ok()` を呼んでいます。

一方、`SYS_WRITE` はユーザー空間から渡された `buf` に対して `proc_user_readable_ok()` を呼ばず、そのまま `fs_write()` へ渡しています。

```c
case SYS_WRITE:
    f->a0 = (uint32_t) fs_write((int) f->a0, (const void *) f->a1, f->a2);
    break;
```

問題点は以下です。

- U-modeから不正ポインタを渡された場合、kernelが直接そのアドレスを読む。
- user VMA外のアドレスを参照する可能性がある。
- kernel trapや予期しないメモリ参照につながる。
- 他のI/O syscallと検証方針が揃っていない。

優先度は高いです。最低限、`f->a2 != 0` の場合に `proc_user_readable_ok(f->a1, f->a2)` を確認するべきです。

### 2. GPU I/Oが同期直列で、全画面転送になっている

`kernel/drivers/virtio_gpu.c` の `virtio_gpu_present()` は、毎回resource全体を転送してflushします。

```c
void virtio_gpu_present(void)
{
    if (!vg_ready)
        return;
    if (vg_cmd_transfer_to_host_2d(vg_resource_id, vg_width_px, vg_height_px) < 0)
        return;
    (void) vg_cmd_resource_flush(vg_resource_id, vg_width_px, vg_height_px);
}
```

問題点は以下です。

- dirty rectを使わず、毎回全画面を転送する。
- 小さな描画変更でも画面全体のI/Oになる。
- `wm_render_gpu()` 側にもdirty rectはあるが、GPU presentでは活用されていない。
- `vg_submit()` がdescriptor 0/1を固定使用するため、GPUコマンドを並列に積めない。

現在の規模では動作確認用として許容できますが、GUIを継続描画する段階では大きなボトルネックになります。

### 3. GPU完了待ちに長いbusy-waitがある

`vg_submit()` は、VirtIO GPUコマンド投入後にまず長いpollingを行います。

```c
int timeout = 20000000;
while (timeout > 0) {
    __sync_synchronize();
    if (vg_last_used != vg_used.idx) break;
    __asm__ __volatile__("nop");
    timeout--;
}
```

その後、process contextであれば `waitq_sleep()` にfallbackします。

問題点は以下です。

- GPU応答が遅い場合にCPU時間を大きく消費する。
- 初期化時や表示更新時の遅延がそのままbusy-waitになる。
- timeout値が固定で、実時間ベースではない。
- 完了通知の基本方針が「まずpolling」になっている。

割り込みとwait queueを主体にし、初期化時だけ最小限のpollingを許す設計の方が安定します。

### 4. Block I/Oにタイムアウトや復旧処理がない

`kernel/drivers/virtio_blk.c` の `blk_read()` はrequestを投入したあと、slotがdoneになるまで待ちます。

```c
while (1) {
    ...
    if (slot->done) {
        ...
        return ok ? 0 : -1;
    }
    ...
    if (current_proc)
        waitq_sleep(&blk_waitq);
    else
        __asm__ __volatile__("wfi");
}
```

問題点は以下です。

- デバイスが応答しない場合に待ち続ける。
- timeoutがない。
- request cancelやdevice resetがない。
- error時にdriver全体を再初期化する経路がない。

I/Oエラーや仮想デバイス停止に弱い構造です。

## ファイルシステムまわり

### 5. ext4実装はあるが通常経路では無効

`kernel/fs/ext4.c` にはread-only ext4実装があります。

実装されている主な機能は以下です。

- superblock読み込み
- group descriptor読み込み
- inode読み込み
- extent読み込み
- root directory scan
- root直下ファイルのlookup/read/listdir

しかし、`kernel/fs/fs.c` の `fs_init()` ではext4 mountをskipしています。

```c
printf("fs: ext4 mount skipped (temporary)\n");
ext4_ready = false;
```

そのため、通常の `fs_open()` / `fs_read()` / `fs_listdir()` からはext4が使われません。

問題点は以下です。

- `fs.ext4` をQEMUに接続していても、通常のFS経路では利用されない。
- ext4 read-only実装の動作確認が起動経路に乗っていない。
- RAMFSだけが実用経路になっている。

### 6. RAMFSの容量と機能がかなり限定的

`kernel/fs/fs.c` のRAMFSは小さな固定配列です。

```c
#define RAMFS_MAX_FILES 32
#define RAMFS_NAME_MAX 32
#define RAMFS_DATA_MAX 1024
```

制限は以下です。

- 最大32ファイル。
- 1ファイル最大1024 bytes。
- ファイル名は短い。
- 階層ディレクトリなし。
- 永続化なし。
- 同名rename先は失敗する。

`fs_write()` は容量を超える場合、書ける分だけ書くか、offsetが上限以上なら0を返します。

```c
if (d->offset >= RAMFS_DATA_MAX)
    return 0;
```

問題点は以下です。

- 通常のファイルI/O用途には容量が小さい。
- write失敗とEOF的な0返却の意味が曖昧になりやすい。
- ファイルサイズ上限到達をエラーとして扱えない。
- ディレクトリやmetadataがない。

### 7. `unlink` が開いているfdを強制的に無効化する

`fs_unlink()` は、対象inodeを参照している全processのfdを探して `used = 0` にします。

```c
for (int p = 0; p < PROC_MAX; p++) {
    for (int fd = 0; fd < FD_MAX; fd++) {
        if (procs[p].fds[fd].used && procs[p].fds[fd].inode == inode)
            procs[p].fds[fd].used = 0;
    }
}
```

問題点は以下です。

- Unix的な「unlink後もopen fdからは読める」挙動ではない。
- 他processのfdが突然無効化される。
- fd利用中のI/Oと競合すると予測しづらい。
- FS全体のlockがないため、将来並行実行が増えると危険。

現在の小規模RAMFSでは単純化として理解できますが、I/O semanticsとしては弱いです。

## VirtIO block

### 8. block driverはread専用

`virtio_blk.c` では `VIRTIO_BLK_T_IN` の読み込みだけが実装されています。

```c
#define VIRTIO_BLK_T_IN 0
```

問題点は以下です。

- block deviceへのwriteができない。
- ext4をmountできたとしてもread-only用途に限られる。
- RAMFSのwrite結果はvirtio-blkへ保存されない。
- shutdown後にファイル変更は残らない。

### 9. request queueの使い方が限定的

VirtIO blockは `VIRTQ_NUM 32`、`BLK_REQ_MAX 8` で、1 requestあたり3 descriptorを使う設計です。

これは最低限の並行readには対応できますが、以下の制限があります。

- scatter/gather readはない。
- request mergeはない。
- 複数sector read APIはない。
- cache層がない。
- read-aheadがない。
- write-back/write-throughのような方針もない。

現状のext4 readは、必要なblockを都度 `blk_read()` するため、metadataやfile dataの繰り返し読み込みが多くなる可能性があります。

## VirtIO input

### 10. 入力イベントが取りこぼされる設計

VirtIO input IRQ handlerは、取得できるイベントをkernel event queueへ積みます。

```c
while (vi_fetch_one_event(&ev, &dev) > 0) {
    kevent_push(KEVENT_TYPE_INPUT, (uint32_t) ev.type, (uint32_t) ev.code, ev.value, 0);
}
```

kernel event queueは固定長128個です。overflow時は最古イベントを捨てます。

問題点は以下です。

- 高頻度入力で古いイベントが落ちる。
- mouse moveやwheelの圧縮はkernel event queue側にはない。
- input deviceごとのqueueではなく、global queueへ混在する。
- どのinput deviceから来たイベントかは利用側へ渡していない。

### 11. シェル入力がkey以外のイベントを捨てる

`kernel/apps/user_init.c` の `u_getchar()` は `KEVENT_TYPE_INPUT` を読みますが、`VI_EV_KEY` 以外は捨てます。

```c
if (ev.type != VI_EV_KEY) continue;
```

問題点は以下です。

- mouse入力やrelative moveはシェル入力経路では失われる。
- WM側の `wm_poll_mouse_input()` と入力イベントの取り合いになりうる。
- 1回の `u_getchar()` で最大32イベントしか処理しない。
- キーボードのmodifier処理はShiftのみ。
- Ctrl/Alt/Escなどの扱いはほぼない。

現在のシェル入力としては最低限ですが、GUIや複数アプリへの入力配送には不足しています。

### 12. WMのマウスpollが通常ループに接続されていない

Window Managerには `wm_poll_mouse_input()` があります。

この関数はVirtIO inputから相対移動と左クリックを読み、カーソル移動やクリックフォーカスを行います。

しかし、現在の `user_init_entry()` のメインループでは `u_wm_poll_mouse()` が定期的に呼ばれていません。

問題点は以下です。

- WM側のマウス処理が継続的に動かない。
- input event queueをシェル入力が先に消費する可能性がある。
- GUIとCLIが同じ入力queueを直接奪い合う構造になっている。

入力subsystemで正規化し、WM/console/アプリへ配送する層が必要です。

## Kernel event queue

### 13. global queue 1本でI/Oイベントを扱っている

`kernel/event/event.c` はglobalな固定長リングバッファです。

問題点は以下です。

- input, GPU IRQなどが同じqueueに入る。
- consumerがイベントを種類で捨てると、他のconsumerが見る前に消える。
- 複数consumerに向かない。
- overflow時は最古イベントを無条件で捨てる。
- event type別のbackpressureや圧縮がない。

現在の単一シェル中心なら動きますが、GUI、複数process、driver通知が増えると破綻しやすい構造です。

## syscall I/O APIの制限

### 14. `read` / `write` にblocking modeやerrnoがない

現在のsyscallは戻り値として `-1` やbyte数を返しますが、詳細なエラー理由は返りません。

問題点は以下です。

- `EFAULT`, `EINVAL`, `EBADF`, `ENOSPC`, `EIO` などを区別できない。
- non-blocking I/Oがない。
- `poll` / `select` 相当がない。
- fd種別がfile中心で、device fdやpipe/socketはない。

将来的にGUIやinputをfdベースにする場合、ここは拡張が必要です。

### 15. device I/Oがfdモデルに統合されていない

現在、GPU/input/eventは専用syscallまたはカーネル関数直接呼び出しです。

- `SYS_GPU_INIT`
- `SYS_GPU_INFO`
- `SYS_GPU_PRESENT`
- `SYS_INPUT_INIT`
- `SYS_INPUT_NEXT_EVENT`
- `SYS_EVENT_POLL`

問題点は以下です。

- `/dev/input` や `/dev/gpu` のような統一I/Oモデルがない。
- `read/write/ioctl/mmap/poll` と統合されていない。
- 権限管理やsession管理がない。
- deviceごとのlifetime管理が曖昧になる。

## 優先して直すべき順序

実装優先度としては、以下の順が妥当です。

1. `SYS_WRITE` に `proc_user_readable_ok()` を追加する。
2. block I/Oにtimeoutとエラー復旧方針を入れる。
3. input queueをconsumer別、または配送層つきに整理する。
4. WMのmouse pollをメインループへ接続するか、input dispatcherを作る。
5. GPU presentにdirty rect指定を渡せるようにする。
6. ext4 mountを通常起動経路へ戻す。
7. RAMFSの容量上限時に明確なエラーを返す。
8. device I/Oをfdまたは明確なdevice APIへ整理する。

## まとめ

現在のI/O実装は、起動確認と基本動作には十分な土台があります。

- RAMFSで簡単なfile I/Oができる。
- VirtIO block readの基礎がある。
- ext4 read-only実装がある。
- VirtIO GPUで画面更新できる。
- VirtIO inputでキー入力を受け取れる。
- kernel event queueでIRQ由来イベントを受け渡せる。

一方で、実用性と安全性の面ではまだ課題があります。

特に `SYS_WRITE` のuser pointer検証漏れは安全性の問題なので、最初に修正すべきです。その次に、I/O timeout、入力配送、GPU dirty transfer、ext4再接続を進めるのが現実的です。
