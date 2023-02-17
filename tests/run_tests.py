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
        '../data') if re.match(r'^.*\.csv$', f)]

    # add all files that end with .txt to the list
    filenames += [f for f in os.listdir(
        '../data') if re.match(r'^.*\.txt$', f)]

    # remove files that contain 6vars
    filenames = [f for f in filenames if not re.match(r'^.*6vars.*$', f)]

    # remove files that contain poly
    filenames = [f for f in filenames if not re.match(r'^.*poly.*$', f)]

    for filename in filenames:
        if PASSED == False:
            break

        print("[*] Testing " + filename + " ...", end='\r')

        # Get filename without extension
        filename_without_extension = os.path.splitext(filename)[0]

        # measure time
        start = time.time()

        i = 1
        with open('../data/' + filename, 'r') as file:
            for line in file:
                # if line starts with # or is empty, skip it
                if line.startswith("#") or line == "\n":
                    continue

                # Get expected output from test file
                expected_output = line.split(",")[1].strip()

                # Get MBA
                mba = line.split(",")[0].strip()
                print("[*] Testing " + filename + " [" + str(i) +
                      "/" + str(MaxTestCount) + "]", end='\r')

                # Get actual output from SiMBA++
                cmd = '..\\build\\SiMBA++ --simplify-expected -fastcheck -bitcount=' + \
                    str(BitCount) + \
                    ' -checklinear=true -signed=' + \
                    str(Signed) + ' -mba \"' + mba + "\""

                output = os.popen(cmd).read()

                # Check if output contains the string "Not Valid Transformation!"
                if "Not valid replacement!" in output or "Input expression may be no linear" in output:
                    print("")
                    print(bcolors.FAIL + "[E] Test " +
                          filename + " failed!" + bcolors.ENDC)
                    print(bcolors.FAIL + "MBA: " + mba + bcolors.ENDC)
                    print(bcolors.FAIL + "Command: " + cmd + bcolors.ENDC)

                    PASSED = False
                    break

                i += 1

                if (i > MaxTestCount):
                    break

        # measure time
        end = time.time()

        print("[*] Testing " + filename +
              " (" + "{:.2f}".format(end - start) + "s)")

    if (PASSED):
        print(bcolors.OKGREEN + "[*] All tests passed!" + bcolors.ENDC)
        sys.exit(0)
    else:
        print(bcolors.FAIL + "[*] Some tests failed!" + bcolors.ENDC)
        sys.exit(1)


main()
