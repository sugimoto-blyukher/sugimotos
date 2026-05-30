FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    clang \
    lld \
    make \
    git \
    ca-certificates \
    file \
    python3 \
    gdb-multiarch \
    qemu-system-misc \
    qemu-system-gui \
    qemu-utils \
    libsdl2-2.0-0 \
    && rm -rf /var/lib/apt/lists/*

USER vscode
