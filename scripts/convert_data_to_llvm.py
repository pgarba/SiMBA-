#!/usr/bin/python3

import sys
import os
import re


def main():
    # Get all filenames from data folder that end with .txt or .csv into a list but not file that start with bitwise_list
    filenames = [f for f in os.listdir(
        '../data') if re.match(r'^(?!bitwise_list).*\.(txt|csv)$', f)]

    filesnames_out = []

    # For each filename execute a command to convert the data to LLVM IR
    for filename in filenames:
        print("Converting " + filename + " to LLVM IR ...")

        # Remove the file extension from the filename
        filename_out = filename.split('.')[0]
        os.system('..\\build\\SiMBA++ --mbadb=..\\data\\' + filename +
                  " --convert-to-llvm=..\\llvm\\" + filename_out + ".ll")

        filesnames_out.append(filename_out)

        # Optimise the LLVM IR
        # os.system('opt -O3 -S -o ../llvm/' + filename_out + '.ll ../llvm/' + filename_out + '.ll')

    # Check if files in filesnames_out exist
    for filename in filesnames_out:
        if not os.path.exists('../llvm/' + filename + '.ll'):
            print("Error: File " + filename + ".ll does not exist!")
            sys.exit(1)


# execute code
main()
