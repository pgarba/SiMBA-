#ifndef SIMPLIFIER_H
#define SIMPLIFIER_H

#include <map>
#include <set>
#include <string>
#include <vector>

#include <cstdint>

#include <llvm/ADT/APInt.h>

#include "splitmix64.h"

using namespace std;

// LSimba namespace
namespace LSiMBA {

class GeneralSimplifier {
private:
  int bitCount;
  bool modRed;
  int verifBitCount;
  int vnumber;
  vector<string> variables;
  int MAX_IT;
  string VNAME_PREFIX;
  string VNAME_SUFFIX;
  int modulus;

  // Get the internal name of the variable with given index.
  string getVname(int i) { return VNAME_PREFIX + to_string(i) + VNAME_SUFFIX; }

  // Reduces the given number modulo modulus.
  int mod_red(int n) {
    if (modRed) {
      return n % modulus;
    }
    return n;
  }

  // Find all variables occuring in the given tree, store them in a list and
  // enumerate the tree's variable nodes accordingly.
  void collectAndEnumerateVariables(Node *node) {
    variables.clear();
    // Get a list of unique variables.
    node->collectAndEnumerateVariables(variables);
    vnumber = variables.size();
  }

  // Get the vector storing results of expression evaluation for all truth
  // value combinations, i.e., [e(0,0,...), e(1,0,...), e(0,1,...), e(1,1,...)].
  vector<int> getResultVector(Node *node) {
    vector<int> resultVector;
    for (int i = 0; i < (1 << vnumber); i++) {
      int n = i;
      vector<int> par;
      for (int j = 0; j < vnumber; j++) {
        par.push_back(n & 1);
        n = n >> 1;
      }
      resultVector.push_back(node->eval(par));
    }
    return resultVector;
  }

  vector<int> get_groupsizes() {
    vector<int> groupsizes;
    groupsizes.resize(1);
    groupsizes[0] = 1;
    for (int i = 1; i < vnumber; ++i)
      groupsizes.push_back(2 * groupsizes.back());

    return groupsizes;
  }

  vector<vector<int>> get_variable_combinations() {
    vector<vector<int>> comb;
    comb.push_back(vector<int>());

    for (int v = 0; v < vnumber; ++v) {
      int size = comb.size();
      for (int i = 0; i < size; ++i) {
        vector<int> e = comb[i];
        e.push_back(v);
        comb.push_back(e);
      }
    }

    return comb;
  }

  string get_basis_expression(int idx) {
    if (idx == 0)
      return "1";

    string res;
    for (int v = 0; v < vnumber; ++v) {
      if ((idx & 1) == 1)
        res += variables[v] + "&";
      idx >>= 1;
    }

    res = res.substr(0, res.size() - 1);
    if (res.find('&') != string::npos)
      res = "(" + res + ")";
    return res;
  }

  bool are_variables_true(int n, const vector<int> &variables) {
    int prev = 0;
    for (int i = 0; i < variables.size(); ++i) {
      n >>= (variables[i] - prev);
      prev = variables[i];
      if ((n & 1) == 0)
        return false;
    }

    return true;
  }

  std::vector<int>
  get_linear_combination(const std::vector<int> &result_vector) {
    std::vector<int> result = result_vector;
    int l = result.size();

    // The constant term.
    int constant = result[0];
    for (int i = 1; i < l; i++)
      result[i] -= constant;

    // Determine all conjunctions of variables (including trivial
    // conjunctions of single variables).
    std::vector<std::vector<int>> combinations = get_variable_combinations();
    std::vector<int> groupsizes = get_groupsizes();
    for (std::vector<int> comb : combinations) {
      int index = 0;
      for (int v : comb)
        index += groupsizes[v];
      int coeff = result[index];

      if (coeff == 0)
        continue;

      subtract_coefficient(result, coeff, index, comb, groupsizes[comb[0]]);
    }

    return result;
  }

  BasisExpression LinearCombination::get_product_linear_combination(
      const BasisExpression &node) const {
    assert(node.get_children().size() == 2 ||
           (node.get_children().size() == 3 &&
            node.get_children()[0].get_type() == NodeType::CONSTANT));

    std::vector<BasisExpression> linCombs;
    for (auto child : std::vector<BasisExpression>(
             {node.get_children()[1], node.get_children()[2]})) {
      linCombs.push_back(get_linear_combination(child));
    }

    BasisExpression res(get_basis_size());
    size_t baselen = 1 << get_variable_number();

    if (node.get_children().size() == 3 &&
        node.get_children()[0].get_type() == NodeType::CONSTANT) {
      for (size_t i = 0; i < linCombs[0].size(); i++) {
        linCombs[0][i] *= node.get_children()[0].get_constant();
      }
    }

    size_t idx = 0;
    for (size_t b = 0; b < baselen; b++) {
      res[idx] = mod_red(linCombs[0][b] * linCombs[1][b]);
      idx++;

      for (size_t a = b + 1; a < baselen; a++) {
        res[idx] = mod_red(linCombs[0][b] * linCombs[1][a] +
                           linCombs[0][a] * linCombs[1][b]);
        idx++;
      }
    }
    assert(idx == res.size());

    return res;
  }

  std::vector<uint64_t> get_power_linear_combination(const Node &node) const {
    assert(node.children.size() == 2 || node.type == NodeType::PRODUCT);

    const Node &base = node.children[0];
    uint64_t coeff = 1;
    if (node.type == NodeType::PRODUCT) {
      assert(node.children[0].type == NodeType::CONSTANT);
      assert(node.children[1].type == NodeType::POWER);
      assert(node.children[1].children[1].type == NodeType::CONSTANT);
      assert(node.children[1].children[1].constant == 2);
      base = node.children[1].children[0];
      coeff = node.children[0].constant;
    } else {
      assert(node.children[1].constant == 2);
    }

    const auto linComb = get_linear_combination(base);

    std::vector<uint64_t> res((1 << (2 * vnumber - 1)) + (1 << (vnumber - 1)));
    const auto baselen = 1 << vnumber;

    size_t idx = 0;
    for (size_t b = 0; b < baselen; ++b) {
      res[idx++] = mod_red(linComb[b] * linComb[b]);

      for (size_t a = b + 1; a < baselen; ++a) {
        res[idx++] = mod_red(2 * linComb[b] * linComb[a]);
      }
    }
    assert(idx == res.size());

    if (coeff != 1) {
      for (size_t i = 0; i < res.size(); ++i) {
        res[i] = mod_red(res[i] * coeff);
      }
    }

    return res;
  }

  std::vector<IndexType> __get_occurring_variable_indices(Node *node) {
    std::vector<IndexType> variables;
    node->collect_variable_indices(variables);
    return variables;
  }

  bool try_simplify_sum_nonlinear_part(Node *node) {
    auto indices = get_indices_of_simple_nonlinear_products_in_sum(node);
    if (indices.size() == 0 || indices.size() == 1) {
      return false;
    }

    collect_and_enumerate_variables(node);

    auto res =
        std::vector<uint64_t>(1 << (2 * vnumber() - 1) + 2 << (vnumber() - 1));
    for (auto i : indices) {
      auto child = node->children[i];
      if (child->type == NodeType::PRODUCT && !child->has_nonlinear_child()) {
        res += get_product_linear_combination(child);
      } else {
        res += get_power_linear_combination(child);
      }
    }
    for (auto i = 0; i < res.size(); ++i) {
      res[i] = mod_red(res[i]);
    }

    auto baselen = 1 << vnumber();

    std::string simpl = "";
    int idx = 0;
    for (int b = 0; b < baselen; b++) {
      if (res[idx] != 0) {
        if (res[idx] != 1) {
          simpl += std::to_string(res[idx]) + "*";
        }
        simpl += this->get_basis_expression(b) + "**2+";
      }
      idx += 1;

      for (int a = b + 1; a < baselen; a++) {
        if (res[idx] != 0) {
          if (res[idx] != 1) {
            simpl += std::to_string(res[idx]) + "*";
          }
          simpl += this->get_basis_expression(b) + "*" +
                   this->get_basis_expression(a) + "+";
        }
        idx += 1;
      }
    }
    assert(idx == res.size());

    if (simpl != "") {
      simpl = simpl.substr(0, simpl.size() - 1);
    }

    for (int i = indices.size() - 1; i > 0; --i) {
      node->children.erase(node->children.begin() + indices[i]);
    }
    if (simpl != "") {
      node->children[indices[0]] = parse(simpl, bitCount, modRed, true, true);
    } else {
      node->children.erase(node->children.begin());
    }

    if (node->children.size() == 1) {
      node->copy(node->children[0]);
    } else if (node->children.size() == 0) {
      node->copy(parse("0", bitCount, modRed, true, true));
    }

    return true;
  }
};