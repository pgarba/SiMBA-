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

# Global variable to check if all tests passed
PASSED = True

# List of all files to test
filenames = ["MBA_OP.ll", "mba_comb.ll", "mbas.ll",
             "vm_obf.ll", "scramble4.ll", "xor_op_pred.ll"]

# Main function


def main():
    # Get all filenames from data folder that end with .csv or .txt
    global PASSED

    # Read bitcount argument from commandline
    if (len(sys.argv) > 1):
        global BitCount
        BitCount = int(sys.argv[1])

    for filename in filenames:
        if PASSED == False:
            break

        print("[*] Testing " + filename + " ...", end='\r')

        # remove .ll from filename
        filename_no_extension = filename[:-3]

        # On non Windows replace \ with /
        if os.name != 'nt':
            filename_no_extension = filename_no_extension.replace("\\", "/")

        # Get actual output from SiMBA++
        cmd = '..\\build\\SiMBA++ -fastcheck -bitcount=' + \
            str(BitCount) + \
            ' -optimize=true -detect-simplify -ir ..\llvm\\' + \
            filename + " -out " + filename_no_extension + ".simplify.ll"

        # On non Windows replace \ with /
        if os.name != 'nt':
            cmd = cmd.replace("\\", "/")
        
        # measure time
        start = time.time()

        output = os.popen(cmd).read()

        # measure time
        end = time.time()

        print("[*] Testing " + filename +
              " (" + "{:.2f}".format(end - start) + "s)")

        # Check if output files exist
        if not os.path.exists(filename_no_extension + ".simplify.ll"):
            print(bcolors.FAIL + "[E] Test " +
                  filename + " failed!" + bcolors.ENDC)
            print(bcolors.FAIL + "Command: " + cmd + bcolors.ENDC)

            PASSED = False
            break

        # Check if output files are smaller than original ones
        SimpFilePath = filename_no_extension + ".simplify.ll"
        OrgFilePath = "..\llvm\\" + filename

        # On non Windows replace \ with /
        if os.name != 'nt':
            SimpFilePath = SimpFilePath.replace("\\", "/")
            OrgFilePath = OrgFilePath.replace("\\", "/")

        if os.path.getsize(SimpFilePath) > os.path.getsize(OrgFilePath):
            print(bcolors.FAIL + "[E] Test " +
                  filename + " failed!" + bcolors.ENDC)
            print(bcolors.FAIL + "Command: " + cmd + bcolors.ENDC)

            PASSED = False
            break

        if "Could not read llvm ir file" in output or "Error: Simplification is not valid for function" in output:
            print("")
            print(bcolors.FAIL + "[E] Test " +
                  filename + " failed!" + bcolors.ENDC)
            print(bcolors.FAIL + "Command: " + cmd + bcolors.ENDC)

            PASSED = False
            break

        # Check if MBAs were found and simplified
        m = re.search(r"\[.\] MBAs found and replaced: '(\d+)'", output)

        if m:
            # get number of MBAs found
            MBA_count = int(m.group(1))

            if MBA_count == 0:
                print(bcolors.FAIL + "[E] Test " +
                      filename + " failed!" + bcolors.ENDC)
                print(bcolors.FAIL + "Command: " + cmd + bcolors.ENDC)

                PASSED = False
                break
        else:
            print(bcolors.FAIL + "[E] Test " +
                  filename + " failed!" + bcolors.ENDC)
            print(bcolors.FAIL + "Command: " + cmd + bcolors.ENDC)

            PASSED = False
            break

    # clean up
    for filename in filenames:
        # remove .ll from filename
        filename_no_extension = filename[:-3]

        # On non Windows replace \ with /
        if os.name != 'nt':
            filename_no_extension = filename_no_extension.replace("\\", "/")

        # remove output file
        if os.path.exists(filename_no_extension + ".simplify.ll"):
            os.remove(filename_no_extension + ".simplify.ll")

    if (PASSED):
        print(bcolors.OKGREEN + "[*] All tests passed!" + bcolors.ENDC)
        sys.exit(0)
    else:
        print(bcolors.FAIL + "[*] Some tests failed!" + bcolors.ENDC)
        sys.exit(1)


main()
