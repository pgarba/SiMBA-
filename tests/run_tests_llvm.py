#!/usr/bin/python3

import sys
import os
import re
import time


class bcolors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'


# Max number of tests to run for each file
MaxTestCount = 50

# Bitcount to use for SiMBA++
BitCount = 64

# Signed or unsigned
Signed = 0

# Global variable to check if all tests passed
PASSED = True

# Main function


def main():
    # Get all filenames from data folder that end with .csv or .txt
    global PASSED

    # Read bitcount argument from commandline
    if (len(sys.argv) > 1):
        global BitCount
        BitCount = int(sys.argv[1])

    # Read signed argument from commandline
    if (len(sys.argv) > 2):
        global Signed
        Signed = int(sys.argv[2])

    filenames = [f for f in os.listdir(
        '../llvm') if re.match(r'^.*\.ll$', f)]

    # remove files that contain 6vars
    filenames = [f for f in filenames if not re.match(r'^.*6vars.*$', f)]

    # remove files that contain 6vars
    filenames = [f for f in filenames if not re.match(r'^.*5vars.*$', f)]

    # remove files that contain poly
    filenames = [f for f in filenames if not re.match(r'^.*poly.*$', f)]

    for filename in filenames:
        if PASSED == False:
            break

        print("[*] Testing " + filename + " ...", end='\r')

        # Get actual output from SiMBA++
        cmd = '..\\build\\SiMBA++ --simplify-expected -fastcheck -bitcount=' + \
            str(BitCount) + \
            ' -checklinear=true -optimize=false -signed=' + \
            str(Signed) + ' -ir ..\llvm\\' + filename + ""

        # measure time
        start = time.time()

        output = os.popen(cmd).read()

        # measure time
        end = time.time()

        print("[*] Testing " + filename +
              " (" + "{:.2f}".format(end - start) + "s)")

        # Check if output contains any errors
        if "Could not read llvm ir file" in output or "Error: Simplification is not valid for function" in output:
            print("")
            print(bcolors.FAIL + "[E] Test " +
                  filename + " failed!" + bcolors.ENDC)
            print(bcolors.FAIL + "Command: " + cmd + bcolors.ENDC)

            PASSED = False
            break

    if (PASSED):
        print(bcolors.OKGREEN + "[*] All tests passed!" + bcolors.ENDC)
        sys.exit(0)
    else:
        print(bcolors.FAIL + "[*] Some tests failed!" + bcolors.ENDC)
        sys.exit(1)


main()
