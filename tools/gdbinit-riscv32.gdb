set pagination off
set confirm off
set disassemble-next-line on
set print pretty on
set print elements 0

file kernel.elf
target remote :1234

# Common early breakpoints
break kernel_main
break handle_trap
break handle_syscall

# Trap quick-inspect helpers
define trapctx
  printf "\n[trap ctx]\n"
  p/x $pc
  p/x $sp
  p/x $ra
  p/x $sstatus
  p/x $sepc
  p/x $scause
  p/x $stval
  p/x $satp
  x/12i $pc
end

define procctx
  printf "\n[current_proc]\n"
  p current_proc
  if current_proc
    p *current_proc
  end
end

define tfctx
  printf "\n[trap_frame @ a0]\n"
  if $a0
    p *(struct trap_frame*)$a0
  else
    printf "a0 is null\n"
  end
end

echo Loaded tools/gdbinit-riscv32.gdb\n
echo Use: continue | trapctx | procctx | tfctx\n
