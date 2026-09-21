#!/usr/bin/env python3
"""Build a separate test kernel and exercise the U-mode syscall path in QEMU."""

from pathlib import Path
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parent.parent
    sources = subprocess.check_output(["make", "-s", "print-sources"], cwd=root, text=True).splitlines()
    sources.remove("kernel/main.c")
    sources.append("tests/syscall_integration.c")
    with tempfile.TemporaryDirectory(prefix="sugimotos-syscall-test-") as temp:
        kernel = Path(temp) / "kernel.elf"
        subprocess.run([
            "clang", "-std=c11", "-O2", "-g3", "-Wall", "-Wextra", "-Werror",
            "--target=riscv32-unknown-elf", "-fuse-ld=lld", "-fno-stack-protector",
            "-ffreestanding", "-nostdlib", "-Iarch/riscv32/include", "-Ikernel/include",
            "-Wl,-Tarch/riscv32/kernel.ld", "-o", str(kernel), *sources,
        ], cwd=root, check=True)
        try:
            result = subprocess.run([
                "qemu-system-riscv32", "-machine", "virt", "-display", "none",
                "-monitor", "none", "-serial", "stdio", "-no-reboot",
                "-bios", "opensbi-riscv32-generic-fw_dynamic.bin", "-kernel", str(kernel),
            ], cwd=root, capture_output=True, text=True, timeout=30)
        except subprocess.TimeoutExpired as error:
            print(error.stdout.decode() if isinstance(error.stdout, bytes) else error.stdout or "")
            raise SystemExit("FAIL: syscall test timed out") from error
        print(result.stdout, end="")
        print(result.stderr, end="")
        if result.returncode or "PASS: syscall integration" not in result.stdout or "FAIL:" in result.stdout:
            raise SystemExit(1)


if __name__ == "__main__":
    main()
