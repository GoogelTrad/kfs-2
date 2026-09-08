FROM i386/ubuntu:latest

RUN apt-get update && apt-get install -y \
    build-essential \
    gcc \
    make \
    qemu-system-x86 \
    grub-pc-bin \
    grub-common \
    xorriso \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /kfs