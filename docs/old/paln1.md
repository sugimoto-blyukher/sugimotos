# 今後の実装方針メモ

現状のコードを見る限り、いきなりユーザプロセス化や syscall 分離を進めるより、まずはカーネル内で動いている shell / GUI / RAMFS の整合性を整える方が楽で適切です。

現在の `user_init_entry` は `create_user_process` ではなく `create_process` で起動されており、実質的にはカーネルスレッドとして動いています。また `kernel/apps/user_app.h` でも syscall wrapper ではなく、`fs_open`、`wm_create`、`putchar` などのカーネル関数を直接呼ぶ形になっています。

そのため、先にユーザプロセス化へ戻すと、FS、WM、入力、GPU、syscall 境界の問題を同時に扱うことになります。まずは既存の直接呼び出し構成のまま、目に見える機能とコードの整合性を固めるのがよいです。

## 1. shell の表示と実装のずれを直す

最初にやるべきことは shell の `help` 表示と実際の command dispatch のずれを直すことです。

現状では `help` に以下のようなコマンドが表示されています。

- `help`
- `ls`
- `cat`
- `touch`
- `write`
- `rm`
- `gui`
- `img`
- `fm`
- `shutdown`
- `exit`

しかし実際に dispatch されているのは主に以下です。

- `help`
- `ls`
- `cat`
- `gui`
- `shutdown`

このずれはユーザーから見ても分かりやすい不整合なので、最初に直す価値があります。

対応方針は二つあります。

- すぐ整えるなら、`help` 表示を実装済みコマンドだけに合わせる。
- OS として機能を増やしたいなら、表示されているコマンドを実装する。

おすすめは後者です。RAMFS の機能はすでにあるため、`touch`、`write`、`rm` は比較的低コストで実装できます。

## 2. RAMFS を使う shell コマンドを増やす

次に、RAMFS の既存機能を shell から使えるようにします。

優先して実装しやすいコマンドは以下です。

- `touch <path>`
  - `O_CREAT` 付きで open して close するだけで実装できる。
- `write <path> <text>`
  - `O_CREAT | O_TRUNC | O_WRONLY` で開いて文字列を書き込む。
- `rm <path>`
  - `fs_unlink` を呼ぶ。
- `mv <old> <new>`
  - 余裕があれば `fs_rename` を呼ぶ形で追加できる。

この段階では RAMFS だけを対象にしてよいです。ext4 は現状 `fs_init` で mount が無効化されているため、ここで無理に扱わない方が安全です。

## 3. `user_img.c` と `user_fm.c` を shell から接続する

`kernel/apps/user_img.c` と `kernel/apps/user_fm.c` は存在していますが、現状の shell dispatch からは直接呼ばれていません。

RAMFS 操作コマンドを整えた後に、以下を接続するとよいです。

- `img <path>`
  - 画像表示機能を起動する。
- `fm`
  - ファイルマネージャを起動する。

この作業は WM、入力、ファイル操作の接続確認にもなります。すでに部品があるため、完全に新規実装するよりも進めやすいです。

## 4. WM API と実装の整理

`include/kernel/wm.h` には多めの API が宣言されています。

一方で、`kernel/gui/wm.c` では一部だけが実装・利用されており、過去の機能や予定機能と思われるものも残っています。

この段階でやるべきことは、全部を一気に作ることではなく、現在使う機能を決めて整理することです。

優先度が高いものは以下です。

- window create
- focus
- text 更新
- image 更新
- render
- event polling
- close

後回しでよいものは以下です。

- drag
- capture
- tile
- dump state
- set visible
- raise

実際に使うものから実装・整理し、使わない宣言は一時的に削るか、未実装であることを明確にした方が後で迷いにくくなります。

## 5. ext4 mount を復帰するか判断する

ext4 読み取り実装自体は存在しています。

ただし現在の `fs_init` では `ext4_mount()` が呼ばれておらず、`ext4_ready` も false にされています。

外部ディスク上のファイルを shell や画像ビューアから読みたいなら、ここで ext4 mount を復帰させる必要があります。

一方で、まず shell / GUI の完成度を上げるだけなら、RAMFS 専用で進めても問題ありません。ext4 は virtio-blk、block cache 的な処理、ディスクイメージの内容なども絡むため、優先度は少し下げてよいです。

## 6. ユーザプロセス化と syscall 分離に戻る

最後に、shell / GUI を本来のユーザプロセスとして動かす方向へ戻します。

この段階で必要になる作業は大きいです。

- `user_init_entry` を `create_user_process` で起動する。
- shell / app 側をカーネル関数直呼びから syscall 呼び出しに戻す。
- FS syscall の read/write/open/close を十分に検証する。
- WM 操作用 syscall を実装するか、別のユーザ API を設計する。
- input event / GPU present / WM render の境界を整理する。
- ユーザポインタ検証を通るように app 側のバッファ配置を確認する。

現状のままこの段階へ入ると、問題が出たときに原因が FS なのか、WM なのか、syscall なのか、VM なのか切り分けにくくなります。

そのため、ここは最後に回すのが適切です。

## 推奨する実装順序

まとめると、次の順番が一番進めやすいです。

1. shell の `help` と dispatch のずれを直す。
2. `touch`、`write`、`rm` など RAMFS 操作コマンドを実装する。
3. `img`、`fm` を shell から起動できるようにする。
4. WM API を、実際に使う機能中心に整理する。
5. 必要になった段階で ext4 mount を復帰する。
6. 最後に shell / GUI のユーザプロセス化と syscall 分離へ進む。

最初の一手としては、`kernel/apps/user_init.c` の shell command dispatch を整備するのがよいです。変更範囲が狭く、既存の RAMFS と WM を活かせるため、動作確認もしやすいです。
