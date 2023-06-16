#!/usr/bin/env python3

# lift and deobfuscate linear_mba.c
import sys
import os
import re


def main():
    # compile the file
    os.system("clang -O0 -o linear_mba linear_mba.c")

    # lift with saturn
    os.system("~/saturn/build/saturn -i linear_mba -f _mba1 -o linear_mba.ll -saturn-print-debug -recover-stack -recover-arguments -saturn-keep-ram=false")

    # deobfuscate with SiMBA
    os.system("../../build/SiMBA++ -ir linear_mba.ll -out linear_mba_deobf.ll -detect-simplify -debug")

    # compile output to object file
    os.system("clang -O3 -c linear_mba_deobf.ll -o linear_mba_deobf.o > /dev/null 2>&1")

    # dump object as assembly
    os.system("objdump -d linear_mba_deobf.o")

# run main
main()
