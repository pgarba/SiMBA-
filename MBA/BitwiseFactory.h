// GAMBA native C++ port — BitwiseFactory.
// Mirrors external/GAMBA/src/bitwise-factory/create_bitwise.py.
#pragma once

#include <string>
#include <vector>

namespace LSiMBA {
namespace MBA {

// Class for creating bitwise expressions for a given number of variables.
class BitwiseFactory {
 public:
  BitwiseFactory(int vnumber, const std::vector<std::string> *variables = nullptr,
                 bool noTable = false);

  // Creates the bitwise expression for the given truth value vector.
  std::string createBitwise(std::vector<int> vector, bool negated = false,
                            int offset = 0);

 private:
  int vnumber;
  std::vector<std::string> variables;
  bool usesDefaultVars;
  std::vector<std::string> table;
  bool tableInit = false;
  bool noTable;

  std::string getAltVname(int i) const;
  void initTable();
  void initTable1var();
  void initTable2vars();
  void initTable3vars();
  std::string createBitwiseImpl(const std::vector<int> &vector);
  std::vector<int> getBitwiseVector(const std::vector<int> &vector, int offset) const;
  int getBitwiseIndexForVector(const std::vector<int> &vector, int offset) const;
  std::string getBitwiseFromTable(const std::vector<int> &vector, int offset);
  std::string createBitwiseWithOffset(const std::vector<int> &vector, int offset);
  std::string createBitwiseUnnegated(const std::vector<int> &vector, int offset = 0);
};

} // namespace MBA
} // namespace LSiMBA
