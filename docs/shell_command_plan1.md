# シェルコマンド実装箇所メモ

この文書は、現状のシェルコマンドがどこに実装されているか、今後コマンドを追加する場合にどこを触るべきかを整理したものです。

## 1. メイン実装場所

シェルコマンドの中心は `kernel/apps/user_init.c` にあります。

主な関数は以下です。

- `shell_help`
  - 場所: `kernel/apps/user_init.c`
  - 役割: `help` コマンドで表示するコマンド一覧を出力する。
  - 現状では `help, ls, cat, touch, write, rm, gui, img, fm, shutdown, exit` を表示している。

- `shell_ls`
  - 場所: `kernel/apps/user_init.c`
  - 役割: `ls` コマンド。
  - 現状では実ファイルシステムの `listdir` ではなく、`g_file_db` という簡易ファイルDBの内容を表示している。

- `shell_cat`
  - 場所: `kernel/apps/user_init.c`
  - 役割: `cat <path>` コマンド。
  - `u_open`、`u_read`、`u_close` を使ってファイル内容を出力する。

- `user_init_entry`
  - 場所: `kernel/apps/user_init.c`
  - 役割: shell の起動点。
  - 入力受付、行バッファ管理、コマンド分割、コマンド dispatch を行う。

## 2. コマンド dispatch の場所

実際にコマンド名を見て処理を分岐しているのは、`user_init_entry` 内の `if / else if` です。

現在の流れは以下です。

1. キー入力を `g_line` に蓄積する。
2. Enter が押されると `g_line` を終端する。
3. `split_args(g_line, argv, SHELL_MAX_ARGS)` で引数配列に分割する。
4. `argv[0]` を `str_eq` で比較してコマンドを実行する。

現在 dispatch されているコマンドは以下です。

- `help`
- `ls`
- `cat`
- `gui`
- `shutdown`

一方で、`help` 表示にはあるが dispatch されていないコマンドは以下です。

- `touch`
- `write`
- `rm`
- `img`
- `fm`
- `exit`

したがって、シェルコマンドを追加する場合は、まず `kernel/apps/user_init.c` の command dispatch に `else if` を追加するのが入口になります。

## 3. 補助関数の場所

シェルで使う文字列処理や履歴処理は `kernel/apps/user_util.c` にあります。

主な関数は以下です。

- `split_args`
  - コマンド行を空白区切りで `argv` に分割する。

- `str_eq`
  - 文字列比較。

- `str_copy_lim`
  - 長さ制限付きコピー。

- `history_init`
- `history_prev`
- `history_next`
- `history_push`
  - コマンド履歴用の関数。

現状の dispatch は `split_args` と `str_eq` に依存しています。

## 4. FS / WM への呼び出し口

`kernel/apps/user_app.h` に、shell 側から使う API 風のマクロが定義されています。

現在は syscall wrapper ではなく、カーネル関数への直接呼び出しです。

主な定義は以下です。

- `u_putchar` -> `putchar`
- `u_getchar` -> `u_getchar`
- `u_yield` -> `yield`
- `u_shutdown` -> `sbi_shutdown`
- `u_exit` -> `proc_exit`
- `u_open` -> `fs_open`
- `u_close` -> `fs_close`
- `u_read` -> `fs_read`
- `u_write` -> `fs_write`
- `u_unlink` -> `fs_unlink`
- `u_rename` -> `fs_rename`
- `u_wm_create` -> `wm_create`
- `u_wm_set_text` -> `wm_set_text`
- `u_wm_focus` -> `wm_focus`
- `u_wm_close` -> `wm_close`
- `u_wm_set_image` -> `wm_set_image`
- `u_wm_render` -> `wm_render`

そのため、現時点で `touch`、`write`、`rm` などを実装する場合は syscall を新しく書く必要はなく、これらの `u_*` 経由で実装できます。

## 5. 画像ビューアとファイルマネージャ

`img` と `fm` に相当する部品はすでに別ファイルにあります。

### `kernel/apps/user_img.c`

- `img_open_path(const char *path)`
  - `ImageViewer` ウィンドウを作成する。
  - 現状では指定された path をテキストとして表示し、実画像の読み込みまではしていない。
  - shell の `img` コマンドにはまだ接続されていない。

### `kernel/apps/user_fm.c`

- `fm_run`
  - `FileManager` ウィンドウを作成または focus する。
  - 現状では簡易テキストを表示する程度。
  - shell の `fm` コマンドにはまだ接続されていない。

- `filedb_add`
- `filedb_remove`
- `filedb_rename`
  - `g_file_db` を操作する補助関数。

## 6. 今後コマンドを追加する時の作業手順

新しい shell コマンドを追加する場合は、基本的に以下の順で進めるとよいです。

1. `kernel/apps/user_init.c` に `shell_xxx` 関数を作る。
2. `user_init_entry` の command dispatch に `else if (str_eq(argv[0], "xxx"))` を追加する。
3. 引数不足時のエラーメッセージを入れる。
4. 必要に応じて `g_file_db` を更新する。
5. `shell_help` の表示を実装済みコマンドと一致させる。
6. QEMU でコマンドを実行して確認する。

## 7. まず追加しやすいコマンド

現状の実装から考えると、まず追加しやすいのは以下です。

- `touch <path>`
  - `u_open(path, O_CREAT | O_RDWR)` して `u_close` する。
  - 成功したら `filedb_add(path)` する。

- `write <path> <text>`
  - `u_open(path, O_CREAT | O_TRUNC | O_WRONLY)` して `u_write` する。
  - 成功したら `filedb_add(path)` する。

- `rm <path>`
  - `u_unlink(path)` を呼ぶ。
  - 成功したら `filedb_remove(path)` する。

- `img <path>`
  - `img_open_path(path)` を呼ぶ。

- `fm`
  - `fm_run()` を呼ぶ。

- `exit`
  - 現在 shell はカーネルスレッドとして動いているため、安易に `u_exit` を呼ぶとシステム全体の挙動確認が必要になる。
  - 最初は `exit` を help から消すか、`shutdown` と同じ扱いにしない方が安全。

## 8. 注意点

- 現在の `ls` は RAMFS の実ディレクトリ一覧ではなく `g_file_db` を表示している。
- `g_file_db` は起動時に `/ext_hello.txt` と `/ext_note.txt` を追加しているが、現状 ext4 mount は無効化されているため、実際に読めるとは限らない。
- `help` 表示と dispatch のずれがあるため、コマンド追加時は必ず両方を更新する。
- 現在の shell はユーザプロセスではなくカーネルスレッドとして動いているため、`u_*` は syscall ではなく直接カーネル関数を呼んでいる。
