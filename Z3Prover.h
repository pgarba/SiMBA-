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

#endif