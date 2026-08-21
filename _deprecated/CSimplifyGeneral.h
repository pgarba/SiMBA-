#ifndef CSIMPLIFYGENERAL_H
#define CSIMPLIFYGENERAL_H

#include <string>
#include <vector>

namespace llvm {
class APInt;
}

namespace LSiMBA {

class SimplifyGeneral {
public:
private:
  std::vector<int64_t> groupsizes;

  int bitCount;

  llvm::APInt modulus;

  bool modRed;

  bool verifBitCount;

  int vnumber = 0;

  std::vector<std::string> variables;

  int MAX_IT = 100;

  std::string VNAME_PREFIX = "Y[";

  std::string VNAME_SUFFIX = "]";

  std::string &get_vname(int i);

  llvm::APInt mod_red(const llvm::APInt &n, bool Signed = false);

  void collect_and_enumerate_variables(void *tree);

  std::vector<llvm::APInt> get_result_vector(void *node);

  std::vector<llvm::APInt> get_groupsizes();

  std::vector<std::vector<llvm::APInt>> get_variable_combinations();

  std::string get_basis_expression();

  bool are_variables_true(llvm::APInt &n, std::vector<llvm::APInt> &variables);
};

}; // namespace LSiMBA
#endif