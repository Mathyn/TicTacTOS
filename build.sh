#!/bin/bash

#Set environment variables
export PREFIX="$HOME/opt/cross"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"
export PATH="$HOME/opt/cross/bin:$PATH"

#Delete the build folder if it already exists
if [ -d "build" ]; then
  rm -rf build
fi

mkdir build
cd build

#compile the boot loader
i686-elf-as ../boot.s -o boot.o

#compile the kernel
i686-elf-gcc -c ../kernel.c -o kernel.o -std=gnu99 -ffreestanding -Wall -Wextra

#link the boot loader and kernel
i686-elf-gcc -T ../linker.ld -o myos.bin -ffreestanding -nostdlib boot.o kernel.o -lgcc

#build the iso
mkdir isodir
mkdir isodir/boot
cp myos.bin isodir/boot/myos.bin
mkdir isodir/boot/grub
cp ../grub.cfg isodir/boot/grub/grub.cfg
grub-mkrescue /usr/lib/grub/i386-pc -o myos.iso isodir
