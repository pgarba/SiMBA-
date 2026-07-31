#ifndef Z3PROVER_H
#define Z3PROVER_H

#include <string>
#include <map>
#include <vector>

#include <z3++.h>

/*
  Prove with Z3 that expr0 == expr1 (expensive!)
*/
bool proveReplacement(std::string &expr0, std::string &expr1, int BitWidth,
                      std::vector<std::string> &Variables);


bool prove(z3::expr conjecture);

/*
  prove() caches one solver for the whole process (see Z3Prover.cpp). That
  solver belongs to the z3::context that created it and calls back into it
  when destroyed, so whoever owns that context must tear the solver down
  *before* freeing it - otherwise the cached pointer dangles into a freed
  context and the next prove() corrupts Z3's heap.
*/
void resetZ3Solver();

/*
  Same, but for use after a hardware fault inside Z3: drops the cached
  solver without running its destructor, since running Z3's own teardown
  over a heap it has already corrupted is what turns a recoverable fault
  into a hard crash.
*/
void abandonZ3Solver();

#endif