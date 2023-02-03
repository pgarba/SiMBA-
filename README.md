# SiMBA++

**SiMBA** (Tool for the simplification of linear mixed Boolean-arithmetic expressions (MBAs)) ported to **C/C++** with some enhancements and multithreading support.

https://github.com/DenuvoSoftwareSolutions/SiMBA

# Multithreading

MBA evaluation and verification will be run in parallel, if the MBA has more than 3 variables.

# General Options

```
  --bitcount=<BitCount>                             - Bitcount of the variables (Default 64)
  --checklinear                                     - Check if MBA is a linear expresssion (Default true)
  --convert-to-llvm=<Database output name>          - Converts the MBA database to LLVM (Default)
  --fastcheck                                       - Verify MBA with random values (Default true)
  --ignore-expected                                 - Ignores the expected string (Default false)
  --ir=<ir>                                         - LLVM Module that contains MBA functions that will be verfied/simplified
  --mba=<mba>                                       - MBA that will be verfied/simplified
  --mbadb=<mbadb>                                   - MBA database that will be verfied/simplified
  --parallel                                        - Evaluate/Check MBA expressions in parallel, give a nice boost on MBA with > 3 vars (Default true)
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
