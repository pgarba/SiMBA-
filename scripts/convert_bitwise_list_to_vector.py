#!/usr/bin/python3

import sys
import os
import re


def main():
    # Get all filenames from data folder that start with bitwise_list and end with .txt
    # and put them into a list
    filenames = [f for f in os.listdir(
        '../data') if re.match(r'^bitwise_list_.*\.txt$', f)]

    # For each filename execute a command to convert the data to LLVM IR
    for filename in filenames:
        print("Converting " + filename + " to std::vector ...")

        # Create a std::vector containing each line without the comment as std::string
        # and separated by a comma in python
        vector = ", ".join(["\"" + line.split("#")[0].strip() +
                           "\"" for line in open('../data/' + filename, 'r')])

        # Get filename without extension
        filename_without_extension = os.path.splitext(filename)[0]

        print("std::vector<std::string> " +
              filename_without_extension + " = {" + vector + "};")


main()
