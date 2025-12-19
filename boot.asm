; boot.asm

[ORG 0x7C00]            ; ブートセクタのロード先アドレス
[BITS 16]               ; 16ビットリアルモードで動作

%include "config.inc"   ; 定数定義

; プログラムの開始点
start:
    xor ax, ax          ; セグメントレジスタを初期化
    mov ds, ax          ; データセグメントを設定
    mov es, ax          ; エクストラセグメントを設定
    mov ss, ax          ; スタックセグメントを設定
    mov sp, 0x7C00      ; スタックポインタを設定

    mov [boot_drive], dl  ; ブートドライブ番号を保存

    ; ディスクからカーネルを読み込む
    mov al, SECTOR_COUNT      ; 読み込むセクタ数
    mov bx, KERNEL_OFFSET>>4  ; ロード先アドレス
    mov es, bx                ; セグメント
    xor bx, bx                ; オフセット
    mov ch, CYLINDER_NUM      ; シリンダ番号
    mov cl, START_SECTOR      ; 開始セクタ番号
    mov dh, HEAD_NUM          ; ヘッド番号
    mov dl, [boot_drive]      ; ドライブ番号
    call read_disk            ; ディスク読み込み    
    
    ; プロテクト（保護）モードへの移行準備
    cli                 ; 割り込みを無効化
    mov ax, 0x2401      ; A20ラインを有効化
    int 0x15            ; BIOS割り込みでA20ラインを有効化
    jc a20_error        ; キャリーフラグ（CF）が立っていたらエラー処理へ
    lgdt [gdt_descriptor]  ; GDTをロード
    mov eax, cr0        ; 保護モードを準備
    or eax, 1           ; 保護モードビットを設定
    mov cr0, eax        ; 保護モードを有効化

    jmp CODE_SEGMENT:pm_start  ; 保護モードへジャンプ

; A20エラー処理
a20_error:
    mov si, a20_err_msg  ; エラーメッセージのアドレスを設定
    call print_string   ; 文字列表示ルーチンを呼び出し
    jmp halt            ; システム停止

%include "utils.inc"    ; 共通処理

; 32ビットモード（プロテクトモード）
[BITS 32]
pm_start:
    mov ax, DATA_SEGMENT  ; データセグメントを設定
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x90000    ; スタックポインタを再設定

    jmp KERNEL_OFFSET   ; カーネルへジャンプ

; GDT（グローバルディスクリプタテーブル）の定義
gdt_start:
    ; ヌルディスクリプタ
    dd 0x00000000
    dd 0x00000000

    ; コードセグメント（0x08）
    dw 0xFFFF         ; セグメントリミット（下位）
    dw 0x0000         ; ベースアドレス（下位）
    db 0x00           ; ベースアドレス（中位）
    db 0x9A           ; アクセスバイト（コード、実行可能、読み取り可能）
    db 0xCF           ; フラグとリミット（上位）（4KB粒度、32ビット）
    db 0x00           ; ベースアドレス（上位）

    ; データセグメント（0x10）
    dw 0xFFFF         ; セグメントリミット（下位）
    dw 0x0000         ; ベースアドレス（下位）
    db 0x00           ; ベースアドレス（中位）
    db 0x92           ; アクセスバイト（データ、書き込み可能）
    db 0xCF           ; フラグとリミット（上位）（4KB粒度、32ビット）
    db 0x00           ; ベースアドレス（上位）
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1  ; GDTのサイズ
    dd gdt_start                ; GDTの開始アドレス

; データ領域
boot_drive   db 0       ; ブートドライブ番号
a20_err_msg  db "A20 enable failed!", 0  ; A20エラーメッセージ

; ブートセクタの調整と署名
times 510-($-$$) db 0   ; 残りをゼロ埋めし、全体を512バイトにする
dw 0xAA55               ; ブートセクタの署名