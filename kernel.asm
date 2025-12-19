; kernel.asm

[ORG 0x1000]            ; カーネルのロード先アドレス
[BITS 32]               ; 32ビットプロテクトモード

; プログラムの開始点
start:
    ; 画面クリア
    mov edi, 0xB8000    ; ビデオメモリの開始アドレス
    mov ax, 0x0720      ; al: スペース（0x20）、ah: 属性（0x07: 白文字/黒背景）
    mov ecx, 2000       ; カウンタレジスタを設定（80x25文字）
    rep stosw           ; AXをECX回EDIに書き込み、EDIをインクリメント

    ; 文字列表示
    mov edi, 0xB8000    ; ビデオメモリの開始アドレス
    mov esi, msg        ; msgアドレスを設定
    call print_string   ; 文字列表示ルーチンを呼び出し

    jmp halt            ; システム停止

; 文字列表示ルーチン
print_string:
    mov ah, 0x07        ; 属性（0x07: 白文字/黒背景）を設定
.loop:
    lodsb               ; SIから1バイトをALに読み込み、SIを進める
    cmp al, 0           ; 文字列終端（NULL文字）をチェック
    je .done            ; 終了
    mov [edi], ax       ; ビデオメモリに書き込み
    add edi, 2          ; 次の位置へ
    jmp .loop           ; 次の文字へ
.done:
    ret                 ; 呼び出し元に戻る

; システム停止
halt:
    cli                 ; 割り込みを無効化
    hlt                 ; CPUを停止
    jmp halt            ; hltから復帰した場合に備えてループ

; データ領域
msg db 'Kernel Loaded!', 0  ; 表示文字列と文字列終端（NULL文字）

; サイズ調整
times 512-($-$$) db 0   ; 1セクタ（512バイト）に調整