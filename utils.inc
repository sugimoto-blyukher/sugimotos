; utils.inc

; ディスク読み込みルーチン
read_disk:
    pusha               ; レジスタ保存
    mov ah, 0x02        ; BIOSのディスク読み込み機能を選択
    int 0x13            ; BIOS割り込みでディスク読み込み
    jc .error           ; キャリーフラグ（CF）が立っていたらエラー処理へ
    popa                ; レジスタ復元
    ret                 ; 呼び出し元に戻る
.error:
    mov si, disk_err_msg  ; エラーメッセージのアドレスを設定
    call print_string   ; 文字列表示ルーチンを呼び出し
    jmp halt            ; システム停止

; 文字列表示ルーチン
print_string:
    pusha               ; レジスタ保存
    mov ah, 0x0E        ; BIOSの文字出力機能 (テレタイプモード)を選択
.loop:
    lodsb               ; SIから1バイトをALに読み込み、SIを進める
    cmp al, 0           ; 文字列終端（NULL文字）をチェック
    je .done            ; 終端ならルーチン終了
    int 0x10            ; BIOS割り込みで文字を表示
    jmp .loop           ; 次の文字へ
.done:
    popa                ; レジスタ復元
    ret                 ; 呼び出し元に戻る

; システム停止
halt:
    cli                 ; 割り込みを無効化
    hlt                 ; CPUを停止
    jmp halt            ; hltから復帰した場合に備えてループ

; データ領域
disk_err_msg db "Disk read error!", 0  ; ディスク読み込みエラーメッセージ