# SiMBA++

```
   _____ __  ______  ___    __    __
  / __(_)  |/  / _ )/ _ |__/ /___/ /_
 _\ \/ / /|_/ / _  / __ /_  __/_  __/
/___/_/_/  /_/____/_/ |_|/_/   /_/
°°SiMBA ported to C/C++/LLVM ~pgarba~
```

**SiMBA** (Tool for the simplification of linear mixed Boolean-arithmetic expressions (MBAs)) ported to **C/C++** with some enhancements and multithreading support. 

* **Able to directly work on LLVM IR!**

Ported from:
https://github.com/DenuvoSoftwareSolutions/SiMBA

# Multithreading

MBA evaluation and verification will be run in parallel, if the MBA has more than 3 variables.

Not supported in LLVM, as LLVM does not support multithreading!

# General Options

```
  --bitcount=<BitCount>                             - Bitcount of the variables (Default 64)
  --checklinear                                     - Check if MBA is a linear expresssion (Default true)
  --convert-to-llvm=<Database output name>          - Converts the MBA database to LLVM
  --fastcheck                                       - Verify MBA with random values (Default true)
  --ignore-expected                                 - Ignores the expected string (Default false)
  --ir=<ir>                                         - LLVM Module that contains MBA functions that will be verfied/simplified
  --mba=<mba>                                       - MBA that will be verfied/simplified
  --mbadb=<mbadb>                                   - MBA database that will be verfied/simplified
  --parallel                                        - Evaluate/Check MBA expressions in parallel, give a nice boost on MBA with > 3 vars (Default true)
  --optimize                                        - Optimize LLVM IR before simplification (Default true)
  --signed                                          - Evaluate as signed values (Default true)
  --simplify-expected                               - Simplify the expected value to match it (Default false)
  --stop=<stop>                                     - Stop after N MBAs are solved (Default 0)
  --z3                                              - Verify MBA with Z3 (Default false)
```

# Performance

Tests are done a MacBook Air M2 24GB

**SiMBA**
```
❯ time python3 simplify_dataset.py -f ../data/e1_2vars.txt
Simplify expressions from ../data/e1_2vars.txt ...
  * ignored: 0
  * total count: 1000
  * verified: 1000
  * equal: 1000
  * average duration: 0.00011201395542593673
python3 simplify_dataset.py -f ../data/e1_2vars.txt  0.18s user 0.01s system 95% cpu 0.201 total

❯ time python3 simplify_dataset.py -f ../data/e1_3vars.txt
Simplify expressions from ../data/e1_3vars.txt ...
  * ignored: 0
  * total count: 1000
  * verified: 1000
  * equal: 1000
  * average duration: 0.0008092961669317446
python3 simplify_dataset.py -f ../data/e1_3vars.txt  0.82s user 0.04s system 94% cpu 0.899 total

❯ time python3 simplify_dataset.py -f ../data/e1_4vars.txt
Simplify expressions from ../data/e1_4vars.txt ...
  * ignored: 0
  * total count: 1000
  * verified: 1000
  * equal: 1000
  * average duration: 0.004005750170443207
python3 simplify_dataset.py -f ../data/e1_4vars.txt  4.06s user 0.02s system 99% cpu 4.091 total


❯ time python3 simplify_dataset.py -f ../data/e1_5vars.txt
Simplify expressions from ../data/e1_5vars.txt ...
  * ignored: 0
  * total count: 1000
  * verified: 1000
  * equal: 1000
  * average duration: 0.05166498399700504
python3 simplify_dataset.py -f ../data/e1_5vars.txt  51.64s user 0.11s system 99% cpu 51.786 total

Simplify expressions from ../../SiMBA-/data/test_data.csv ...
  * ignored: 0
  * total count: 10000
  * verified: 10000
  * equal: 10000
  * average duration: 0.000576711660635192
python3 simplify_dataset.py -f ../../SiMBA-/data/test_data.csv  7.28s user 0.24s system 95% cpu 7.881 total
```

**SiMBA++**
```
❯ time ./SiMBA++ --mbadb ../data/e1_2vars.txt --checklinear=false -fastcheck=false -parallel=true
SiMBA++ - SiMBA ported to C/C++

MBAs: 1000 -> Counter: 1000 Valid: 1000 time: 49ms avg: 0.049000ms
./SiMBA++ --mbadb ../data/e1_2vars.txt --checklinear=false -fastcheck=false   0.05s user 0.00s system 95% cpu 0.057 total

❯ time ./SiMBA++ --mbadb ../data/e1_3vars.txt --checklinear=false -fastcheck=false -parallel=true
SiMBA++ - SiMBA ported to C/C++

MBAs: 1000 -> Counter: 1000 Valid: 1000 time: 307ms avg: 0.307000ms
./SiMBA++ --mbadb ../data/e1_3vars.txt --checklinear=false -fastcheck=false   0.19s user 0.05s system 75% cpu 0.315 total

❯ time ./SiMBA++ --mbadb ../data/e1_4vars.txt --checklinear=false -fastcheck=false -parallel=true
SiMBA++ - SiMBA ported to C/C++

MBAs: 1000 -> Counter: 1000 Valid: 1000 time: 465ms avg: 0.465000ms

❯ time ./SiMBA++ --mbadb ../data/e1_5vars.txt --checklinear=false -fastcheck=false -parallel=true
SiMBA++ - SiMBA ported to C/C++

MBAs: 1000 -> Counter: 1000 Valid: 1000 time: 3575ms avg: 3.575000ms
./SiMBA++ --mbadb ../data/e1_5vars.txt --checklinear=false -fastcheck=false   15.61s user 1.11s system 465% cpu 3.591 total

time ./SiMBA++ --mbadb ../data/test_data.csv --checklinear=false -fastcheck=false -parallel=true --ignore-expected -bitcount=32 -signed=false
SiMBA++ - SiMBA ported to C/C++

MBAs: 10000 -> Counter: 10000 Valid: 10000 time: 1828ms avg: 0.182800ms
./SiMBA++ --mbadb ../data/test_data.csv --checklinear=false -fastcheck=false   1.50s user 0.72s system 120% cpu 1.837 total
```

**SiMBA++ on LLVM IR**
```
[+] Loading LLVM Module: '../llvm/e1_2vars.ll'
[+] Running LLVM optimizer ...		 Done! (319 ms)
[+] Simplifying 1000 functions ...	Done! (8 ms)
[+] MBAs: '1000' time: 8ms
./SiMBA++ -ir ../llvm/e1_2vars.ll -optimize=true -fastcheck=false   0.36s user 0.01s system 98% cpu 0.365 total

[+] Loading LLVM Module: '../llvm/e1_3vars.ll'
[+] Running LLVM optimizer ...		 Done! (793 ms)
[+] Simplifying 1000 functions ...	Done! (34 ms)
[+] MBAs: '1000' time: 34ms
./SiMBA++ -ir ../llvm/e1_3vars.ll -optimize=true -fastcheck=false   0.88s user 0.01s system 99% cpu 0.900 total

[+] Loading LLVM Module: '../llvm/e1_4vars.ll'
[+] Running LLVM optimizer ...		 Done! (4670 ms)
[+] Simplifying 1000 functions ...	Done! (325 ms)
[+] MBAs: '1000' time: 327ms
./SiMBA++ -ir ../llvm/e1_4vars.ll -optimize=true -fastcheck=false   5.21s user 0.03s system 99% cpu 5.259 total

[+] Loading LLVM Module: '../llvm/e1_5vars.ll'
[+] Running LLVM optimizer ...		 Done! (49042 ms)
[+] Simplifying 1000 functions ...	Done! (2889 ms)
[+] MBAs: '1000' time: 2914ms
./SiMBA++ -ir ../llvm/e1_5vars.ll -optimize=true -fastcheck=false   53.55s user 0.27s system 99% cpu 53.928 total

[+] Loading LLVM Module: '../llvm/e5_4vars.ll'
[+] Running LLVM optimizer ...		 Done! (4616 ms)
[+] Simplifying 1000 functions ...	Done! (326 ms)
[+] MBAs: '1000' time: 329ms
./SiMBA++ -ir ../llvm/e5_4vars.ll -optimize=true -fastcheck=false   5.16s user 0.03s system 99% cpu 5.189 total

[+] Loading LLVM Module: '../llvm/e1_6vars.ll'
[+] Running LLVM optimizer ...		 Done! (286093 ms)
[+] Simplifying 1000 functions ...	Done! (21532 ms)
[+] MBAs: '1000' time: 21668ms
./SiMBA++ -ir ../llvm/e1_6vars.ll -optimize=true -fastcheck=false   315.80s user 1.45s system 99% cpu 5:17.85 total

[+] Loading LLVM Module: '../llvm/test_data.ll'
[+] Running LLVM optimizer ...		 Done! (7619 ms)
[+] Simplifying 10000 functions ...	Done! (470 ms)
[+] MBAs: '10000' time: 477ms
./SiMBA++ -ir ../llvm/test_data.ll -optimize=true -fastcheck=false     8.40s user 0.06s system 99% cpu 8.486 total
```
