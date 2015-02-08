#!/bin/bash

sudo bash build.sh

sudo qemu-system-i386 -m 1G -cdrom build/myos.iso

