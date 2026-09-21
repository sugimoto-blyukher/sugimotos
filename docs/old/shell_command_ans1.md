# シェルコマンド再実装差分

`docs/shell_command_plan1.md` の計画に沿って、シェルコマンド実装を整理し直した。

## 変更ファイル

- `kernel/apps/user_init.c`
- `kernel/apps/user_fm.c`

## 実装内容

### `help`

- `exit` は現状の shell がカーネルスレッドで動いており、安全に扱う設計がまだないため、`help` 表示から外した。
- 表示コマンドを実装済みのものに合わせた。

現在の表示:

```text
commands: help, ls, cat, touch, write, rm, gui, img, fm, shutdown
```

### `touch <path>`

- `u_open(path, O_CREAT | O_RDWR)` でファイルを作成または open するようにした。
- open に失敗した場合は `touch: fail` を表示する。
- 成功後に `u_close` し、`filedb_add(path)` で `ls` 用の簡易 DB に追加する。
- 以前入っていた、`touch` 実行後にファイル一覧を勝手に表示する副作用は削除した。

### `write <path> <text>`

- `u_open(path, O_CREAT | O_TRUNC | O_WRONLY)` で作成・上書きするようにした。
- open 失敗時のチェックを復活させた。
- `argv[2]` だけでなく、`argv[2]` 以降を空白区切りで連結して書き込むようにした。
- 成功後に `filedb_add(path)` する。

例:

```text
write /memo hello world
```

この場合、`hello world` が書き込まれる。

### `rm <path>`

- 以前は open/close と `filedb_remove` だけで、実ファイルを削除していなかった。
- `u_unlink(path)` を呼ぶように修正した。
- unlink 成功後に `filedb_remove(path)` する。

### `img <path>`

- 以前は `u_wm_create("ImageViewer", 320, 240)` だけを呼び、未使用変数 `win` が残っていた。
- `img_open_path(path)` を呼ぶように修正した。
- 引数不足時は `usage: img <path>` を表示する。
- 失敗時は `img: fail` を表示する。

### `fm`

- `fm_run()` を呼ぶ形で接続済み。
- 関数宣言を `void shell_fm(void)` に整理した。

### 引数不足時のメッセージ

以下のコマンドで usage を出すようにした。

- `cat <path>`
- `touch <path>`
- `write <path> <text>`
- `img <path>`
- `rm <path>`

### `filedb_add`

- `g_file_db` に同じ path が重複登録されないようにした。
- `touch` や `write` を同じファイルに複数回実行しても、`ls` に同じ path が重複表示されない。

## 削除・整理したもの

- 未計画かつ `help` に出していなかった `echo` dispatch と `shell_echo` を削除した。
- `img` の未使用変数 `win` を削除した。
- `write` のコメントアウトされた失敗処理を削除し、通常のエラー処理として実装した。

## 確認結果

以下のビルドコマンドで確認した。

```sh
clang -std=c11 -O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf -fuse-ld=lld -fno-stack-protector -ffreestanding -nostdlib -Iinclude -Wl,-Tkernel/arch/riscv32/kernel.ld -Wl,-Map=kernel.map -o kernel.elf $(find kernel lib -name "*.c")
```

結果:

- ビルド成功
- 警告なし

## 主な差分

```diff
- u_puts("commands: help, ls, cat, touch, write, rm, gui, img, fm, shutdown, exit\n");
+ u_puts("commands: help, ls, cat, touch, write, rm, gui, img, fm, shutdown\n");
```

```diff
+ void shell_touch(const char *path) {
+     int fd = u_open(path, O_CREAT | O_RDWR);
+     if (fd < 0) {
+         u_puts("touch: fail\n");
+         return;
+     }
+     u_close(fd);
+     filedb_add(path);
+ }
```

```diff
+ void shell_rm(const char *path) {
+     if (u_unlink(path) < 0) { u_puts("rm: fail\n"); return; }
+     filedb_remove(path);
+ }
```

```diff
+ void shell_write(const char *path, int argc, char **argv) {
+     int fd = u_open(path, O_CREAT | O_TRUNC | O_WRONLY);
+     if (fd < 0) { u_puts("write: fail\n"); return; }
+     for (int i = 2; i < argc; i++) {
+         if (i > 2)
+             u_write(fd, " ", 1);
+         u_write(fd, argv[i], str_len(argv[i]));
+     }
+     u_close(fd);
+     filedb_add(path);
+ }
```

```diff
+ void shell_img(const char *path) {
+     if (img_open_path(path) < 0)
+         u_puts("img: fail\n");
+ }
```

```diff
+ void filedb_add(const char *path) {
+     for (int i = 0; i < g_file_db_count; i++) {
+         if (str_eq(g_file_db[i], path))
+             return;
+     }
+     if (g_file_db_count >= FILE_DB_MAX) return;
+     str_copy_lim(g_file_db[g_file_db_count++], path, FM_NAME_MAX);
+ }
```
