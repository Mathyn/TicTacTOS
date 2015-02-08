# TicTacTOS

## Build instructions
This OS was primarily developed on Ubuntu. This guide will work best on Linux systems. 

First of all you will need to build a Cross-Compiler to compile the source. Any existing C compilers on your system will most likely not work. Please do not try them.

Use the following guide to compile your own Cross-Compiler: http://wiki.osdev.org/GCC_Cross-Compiler

Once you've build the Cross-Compiler you can try and run the shell script 'build.sh' in order to compile the OS. A build folder will appear with the OS compiled as an ISO.

Depending on how you build your Cross-Compiler you may need to edit some build parameters. To do so open the file 'build.sh' and edit the lines starting with 'export'.

## Run the OS using QEMU
In order to run the OS using the virtual machine software QEMU you will first need to install QEMU (http://wiki.qemu.org/Download).

Once you've installed QEMU you can run and build the OS by running the shell script 'run.sh'.



