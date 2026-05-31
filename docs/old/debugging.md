# Debugging Playbook (RISC-V / QEMU)

## 1. Build
```bash
./run.sh cui
```

## 2. Start QEMU in GDB wait mode
別ターミナルで QEMU を起動するときは `-S -s` を付けます（`run.sh` を改造するか手動実行）。

例:
```bash
qemu-system-riscv32 -machine virt -bios default -kernel kernel.elf -S -s -nographic
```

## 3. Attach GDB
```bash
riscv64-unknown-elf-gdb -x tools/gdbinit-riscv32.gdb
```

## 4. Frequently used commands
- `continue`: 実行再開
- `trapctx`: trap系 CSR と命令列を表示
- `procctx`: `current_proc` の状態を表示
- `tfctx`: `a0` が指す `struct trap_frame` を表示

## 5. Panic / Trap output format
カーネルは以下の情報を出します。
- `PANIC_CTX`: `scause stval sepc satp sstatus`
- `PANIC_PROC`: `pid state is_user sp proc_sepc proc_satp`
- `trap` / `trap proc` / `trap tf*`: trap時のCSR・プロセス・レジスタ退避内容

## 6. Investigation order
1. `scause/stval/sepc` を確認
2. `sepc` 付近を逆アセンブル（`x/12i $pc`）
3. `current_proc` と `satp` を確認
4. page faultなら `fault VA(stval)` と VMA/PTE の対応を確認
