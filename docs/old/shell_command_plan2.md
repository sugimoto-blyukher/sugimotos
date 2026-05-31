# 次に実装するシェル周辺の計画

次に実装すべきなのは、`ls` を `g_file_db` ベースから実ファイルシステムの `fs_listdir()` ベースへ寄せることです。

## 背景

現在、`touch`、`write`、`rm` は実際の RAMFS に対してファイル操作を行うようになっています。

一方で、`ls` はまだ `g_file_db` という shell 側の簡易ファイルDBを表示しています。

この状態だと、ファイルシステム上の実体と shell の一覧表示がずれる可能性があります。

例:

- ファイルは RAMFS に存在するが、`g_file_db` に登録されていないため `ls` に出ない。
- `g_file_db` にだけ残っていて、実際には読めないファイルが `ls` に出る。
- `rm` や `write` の挙動と `ls` の表示が一致しない。

そのため、次は `ls` を実 FS の一覧取得に合わせるのが適切です。

## 1. `u_listdir` を追加する

まず `kernel/apps/user_app.h` に `fs_listdir` への wrapper を追加します。

現在は以下のような FS wrapper があります。

- `u_open`
- `u_close`
- `u_read`
- `u_write`
- `u_unlink`
- `u_rename`

ここに以下を追加します。

```c
#define u_listdir fs_listdir
```

これにより、shell 側から `u_listdir()` として実 FS の一覧取得を呼べるようになります。

## 2. `shell_ls()` を `u_listdir()` ベースに変更する

現在の `shell_ls()` は `g_file_db` を表示しています。

変更後は、`g_iobuf` など既存の I/O バッファに `u_listdir()` の結果を書き込ませ、それを出力する形にします。

想定実装:

```c
void shell_ls(void) {
    int n = u_listdir(g_iobuf, sizeof(g_iobuf));
    if (n < 0) {
        u_puts("ls: fail\n");
        return;
    }
    for (int i = 0; i < n; i++)
        u_putchar(g_iobuf[i]);
}
```

これにより、`ls` は RAMFS の実体に基づいた一覧になります。

## 3. `g_file_db` の用途を見直す

`ls` が実 FS ベースになれば、`g_file_db` を shell の正規のファイル一覧として使う必要は薄くなります。

ただし、すぐに完全削除する必要はありません。

選択肢は以下です。

- ファイルマネージャ用のキャッシュとして残す。
- `fm_run()` を実 FS ベースにした後で削除する。
- `touch`、`write`、`rm` から `filedb_add/remove` 呼び出しを外す。

まずは `ls` だけを実 FS ベースに変え、`g_file_db` は一旦残してよいです。変更範囲を小さくできます。

## 4. `fm_run()` を実 FS 一覧表示にする

次の段階では、`kernel/apps/user_fm.c` の `fm_run()` を改善します。

現状は固定テキストを表示するだけです。

```text
File Manager (Direct Kernel Access)
Click to select files.
```

ここを `u_listdir()` の結果を使って、現在の RAMFS 一覧を表示するようにします。

想定方針:

- `fm_run()` 内で `u_listdir()` を呼ぶ。
- 結果を `g_textbuf` などに整形する。
- `u_wm_set_text(g_fm.win_id, g_textbuf)` で表示する。

これにより、`fm` コマンドを実行したときに実ファイル一覧が GUI に出るようになります。

## 5. `img <path>` の実装を少し進める

`img <path>` は現在 `img_open_path(path)` に接続されていますが、`img_open_path()` は指定 path をテキストとして表示するだけです。

次の改善候補は、まず画像デコードではなく、ファイル読み込み確認を入れることです。

例:

- `read_file_all(path, g_img_buf, IMG_BUF_MAX)` で読み込む。
- 読み込みに失敗したら `img: fail`。
- 読み込めた byte 数や先頭数 byte の情報を ImageViewer に表示する。

画像フォーマットの本格対応は後回しでよいです。まず `img` が実ファイルを読みに行く形にすると、FS と GUI の接続確認になります。

## 推奨する実装順序

次の順番が進めやすいです。

1. `kernel/apps/user_app.h` に `u_listdir` を追加する。
2. `kernel/apps/user_init.c` の `shell_ls()` を `u_listdir()` ベースに変更する。
3. ビルドして警告がないことを確認する。
4. `fm_run()` を `u_listdir()` ベースの表示に変更する。
5. `img_open_path()` に実ファイル読み込み確認を追加する。
6. その後、`g_file_db` を残すか削るか判断する。

最初の一手としては、`u_listdir` の追加と `shell_ls()` の変更だけで十分です。変更範囲が小さく、`touch/write/rm/ls/cat` の基本的な整合性がかなり良くなります。
