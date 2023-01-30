# SiMBA++

SiMBA ported to C/C++ with some enhancements and multithreading support.

# Multithreading

Evaluation and MBA verification will be run in parallel, if MBA has more than 3 variables.

# General Options

  --bitcount=<BitCount> - Bitcount of the variables (Default 64)
  --checklinear         - Check if MBA is a linear expresssion (Default true)
  --fastcheck           - Verify MBA with random values (Default true)
  --ignore-expected     - Ignores the expected string (Default false)
  --mba=<mba>           - MBA that will be simplified
  --mbadb=<mbadb>       - MBA database that will be verfied/simplified
  --parallel            - Evaluate/Check MBA expressions in parallel, give a nice boost on MBA with > 3 vars (Default true)
  --signed              - Evaluate as signed values (Default true)
  --stop=<stop>         - Stop after N MBAs (Default 0)
