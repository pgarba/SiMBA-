// GAMBA native C++ port — expand / factorize_sums.
// Mirrors external/GAMBA/src/utils/node.py.
#include "Node.h"

#include <algorithm>

namespace LSiMBA {
namespace MBA {

static constexpr int64_t MAX_EXPONENT_TO_EXPAND = 2;

// --------------------------------------------------------- expand
bool Node::expand(bool restrictedScope) {
  if (restrictedScope && type != NodeType::SUM && type != NodeType::PRODUCT &&
      type != NodeType::POWER)
    return false;

  bool changed = false;
  if (restrictedScope && type == NodeType::POWER)
    changed = children[0]->expand(restrictedScope);
  else
    for (auto &c : children)
      if (c->expand(restrictedScope))
        changed = true;

  if (changed) {
    inspectConstants();
    if (type == NodeType::SUM)
      flattenBinaryGeneric();
  }

  if (checkExpand())
    changed = true;
  if (changed && type == NodeType::SUM)
    mergeSimilarNodesSum();

  return changed;
}

bool Node::checkExpand() {
  if (type == NodeType::PRODUCT)
    return checkExpandProduct();
  if (type == NodeType::POWER)
    return checkExpandPower();
  return false;
}

bool Node::checkExpandProduct() {
  if (children.size() == 1)
    return false;
  if (!hasSumChild())
    return false;

  expandProduct();
  return true;
}

bool Node::hasSumChild() const { return getFirstSumIndex() != -1; }

int Node::getFirstSumIndex() const {
  for (size_t i = 0; i < children.size(); ++i)
    if (children[i]->type == NodeType::SUM)
      return static_cast<int>(i);
  return -1;
}

void Node::expandProduct() {
  std::shared_ptr<Node> node = nullptr;
  while (true) {
    int sumIdx = getFirstSumIndex();
    if (sumIdx == -1)
      break;

    node = children[sumIdx]->getCopy();

    bool repeat = false;
    for (size_t i = 0; i < children.size(); ++i) {
      if (static_cast<int>(i) == sumIdx)
        continue;
      node->multiplySum(children[i]);

      if (node->isConstant(0))
        break;

      if (node->type != NodeType::SUM) {
        children[sumIdx] = node;
        for (int j = static_cast<int>(i); j >= 0; --j) {
          if (j == sumIdx)
            continue;
          children.erase(children.begin() + j);
        }
        repeat = true;
        break;
      }
    }

    if (!repeat)
      break;
  }

  if (node != nullptr) {
    if (node->children.size() == 1)
      copy(*node->children[0]);
    else
      copy(*node);
  }
}

void Node::multiplySum(const std::shared_ptr<Node> &other) {
  if (other->type == NodeType::SUM) {
    multiplySumWithSum(other, true);
    return;
  }

  int64_t constant = 0;

  for (int i = static_cast<int>(children.size()) - 1; i >= 0; --i) {
    auto child = children[i];
    child->multiplyWithNodeNoSum(other);

    if (child->type == NodeType::CONSTANT) {
      constant = static_cast<int64_t>(
          static_cast<uint64_t>(constant) + MBAOps::toLow64(child->constant, bitCount));
      if (i > 0)
        children.erase(children.begin() + i);
      continue;
    }
  }

  if (children[0]->type == NodeType::CONSTANT) {
    if (constant == 0)
      children.erase(children.begin());
    else
      children[0]->setAndReduceConstant(constant);
  } else if (constant != 0) {
    children.insert(children.begin(), newConstantNode(constant));
  }

  mergeSimilarNodesSum();

  if (type == NodeType::SUM && children.empty())
    copy(*newConstantNode(0));
}

void Node::multiplySumWithSum(const std::shared_ptr<Node> &other, bool keepSum) {
  std::vector<std::shared_ptr<Node>> saved = children;
  children.clear();

  for (auto &child : saved) {
    for (auto &ochild : other->children) {
      auto prod = child->getProductWithNode(*ochild);

      if (prod->type == NodeType::CONSTANT) {
        if (prod->isConstant(0))
          continue;

        if (!children.empty() && children[0]->type == NodeType::CONSTANT) {
          children[0]->constant =
              children[0]->getReducedConstant(children[0]->constant + prod->constant);
          continue;
        }

        children.insert(children.begin(), prod);
        continue;
      }

      children.push_back(prod);
    }
  }

  mergeSimilarNodesSum();

  if (children.size() == 1) {
    if (!keepSum)
      copy(*children[0]);
  } else if (children.empty()) {
    copy(*newConstantNode(0));
  }
}

std::shared_ptr<Node> Node::getProductWithNode(const Node &other) const {
  if (type == NodeType::CONSTANT)
    return getProductOfConstantAndNode(other);

  if (other.type == NodeType::CONSTANT)
    return other.getProductOfConstantAndNode(*this);

  if (type == NodeType::PRODUCT) {
    if (other.type == NodeType::PRODUCT)
      return getProductOfProducts(other);
    if (other.type == NodeType::POWER)
      return getProductOfProductAndPower(other);
    return getProductOfProductAndOther(other);
  }

  if (type == NodeType::POWER) {
    if (other.type == NodeType::POWER)
      return getProductOfPowers(other);
    if (other.type == NodeType::PRODUCT)
      return other.getProductOfProductAndPower(*this);
    return getProductOfPowerAndOther(other);
  }

  if (other.type == NodeType::PRODUCT)
    return other.getProductOfProductAndOther(*this);
  if (other.type == NodeType::POWER)
    return other.getProductOfPowerAndOther(*this);
  return getProductGeneric(other);
}

std::shared_ptr<Node> Node::getProductOfConstantAndNode(const Node &other) const {
  auto node = other.getCopy();
  node->multiply(MBAOps::toLow64(constant, bitCount));
  return node;
}

std::shared_ptr<Node> Node::getProductOfProducts(const Node &other) const {
  auto node = getCopy();
  for (auto &ochild : other.children) {
    if (ochild->type == NodeType::CONSTANT) {
      if (node->children[0]->type == NodeType::CONSTANT) {
        node->children[0]->constant = node->children[0]->getReducedConstant(
            node->children[0]->constant * ochild->constant);

        if (node->children[0]->isConstant(0))
          return node->children[0];

      } else {
        node->children.insert(node->children.begin(), ochild->getCopy());
      }
      continue;
    }

    if (ochild->type == NodeType::POWER) {
      node->mergePowerIntoProduct(*ochild);
      continue;
    }

    bool merged = false;
    for (size_t i = 0; i < node->children.size(); ++i) {
      auto child = node->children[i];
      if (child->equals(*ochild)) {
        node->children[i] =
            newNodeWithChildren(NodeType::POWER, {child, newConstantNode(2)});
        merged = true;
        break;
      }
    }

    if (merged)
      continue;

    node->children.push_back(ochild->getCopy());
  }

  if (node->children[0]->isConstant(1))
    node->children.erase(node->children.begin());

  if (node->children.size() == 1)
    return node->children[0];
  if (node->children.empty())
    return newConstantNode(1);
  return node;
}

void Node::mergePowerIntoProduct(const Node &other) {
  auto base = other.children[0];

  for (size_t i = 0; i < children.size(); ++i) {
    auto child = children[i];

    if (child->equals(*base)) {
      children[i] = other.getCopy();
      children[i]->children[1]->addConstant(1);

      if (children[i]->children[1]->isConstant(0))
        children.erase(children.begin() + i);
      return;
    }

    if (child->type == NodeType::POWER && child->children[0]->equals(*base)) {
      child->children[1]->add(other.children[1]);

      if (child->children[1]->isConstant(0))
        children.erase(children.begin() + i);
      return;
    }
  }

  children.push_back(other.getCopy());
}

std::shared_ptr<Node> Node::getProductOfPowers(const Node &other) const {
  if (children[0]->equals(*other.children[0])) {
    auto node = getCopy();
    node->children[1]->add(other.children[1]);

    if (node->children[1]->isConstant(0))
      return newConstantNode(1);

    if (node->children[1]->isConstant(1))
      return node->children[0];

    return node;
  }

  return newNodeWithChildren(NodeType::PRODUCT, {getCopy(), other.getCopy()});
}

std::shared_ptr<Node> Node::getProductOfProductAndPower(const Node &other) const {
  auto node = getCopy();
  node->mergePowerIntoProduct(other);

  if (node->children.size() == 1)
    return node->children[0];
  if (node->children.empty())
    return newConstantNode(1);
  return node;
}

std::shared_ptr<Node> Node::getProductOfProductAndOther(const Node &other) const {
  auto node = getCopy();

  for (size_t i = 0; i < node->children.size(); ++i) {
    auto child = node->children[i];

    if (child->equals(other)) {
      node->children[i] =
          newNodeWithChildren(NodeType::POWER, {child->getCopy(), newConstantNode(2)});
      return node;
    }

    if (child->type == NodeType::POWER && child->children[0]->equals(other)) {
      child->children[1]->addConstant(1);

      if (child->children[1]->isConstant(0)) {
        node->children.erase(node->children.begin() + i);
        if (node->children.size() == 1)
          node = children[0];
      }
      return node;
    }
  }

  node->children.push_back(other.getCopy());
  return node;
}

std::shared_ptr<Node> Node::getProductOfPowerAndOther(const Node &other) const {
  if (children[0]->equals(other)) {
    auto node = getCopy();
    node->children[1]->addConstant(1);

    if (node->children[1]->isConstant(0))
      return newConstantNode(1);
    return node;
  }

  return newNodeWithChildren(NodeType::PRODUCT, {getCopy(), other.getCopy()});
}

std::shared_ptr<Node> Node::getProductGeneric(const Node &other) const {
  if (equals(other))
    return newNodeWithChildren(NodeType::POWER, {getCopy(), newConstantNode(2)});
  return newNodeWithChildren(NodeType::PRODUCT, {getCopy(), other.getCopy()});
}

void Node::multiplyWithNodeNoSum(const std::shared_ptr<Node> &other) {
  if (type == NodeType::CONSTANT) {
    copy(*getProductOfConstantAndNode(*other));
    return;
  }

  if (other->type == NodeType::CONSTANT) {
    multiply(MBAOps::toLow64(other->constant, bitCount));
    return;
  }

  if (type == NodeType::PRODUCT) {
    if (other->type == NodeType::PRODUCT)
      multiplyProductWithProduct(other);
    else if (other->type == NodeType::POWER)
      multiplyProductWithPower(other);
    else
      multiplyProductWithOther(other);
    return;
  }

  if (type == NodeType::POWER) {
    if (other->type == NodeType::POWER)
      multiplyPowerWithPower(other);
    else if (other->type == NodeType::PRODUCT)
      copy(*other->getProductOfProductAndPower(*this));
    else
      multiplyPowerWithOther(other);
    return;
  }

  if (other->type == NodeType::PRODUCT)
    copy(*other->getProductOfProductAndOther(*this));
  else if (other->type == NodeType::POWER)
    copy(*other->getProductOfPowerAndOther(*this));
  else
    multiplyGeneric(other);
}

void Node::multiplyProductWithProduct(const std::shared_ptr<Node> &other) {
  for (auto &ochild : other->children) {
    if (ochild->type == NodeType::CONSTANT) {
      if (children[0]->type == NodeType::CONSTANT) {
        children[0]->constant =
            children[0]->getReducedConstant(children[0]->constant * ochild->constant);

        if (children[0]->isConstant(0)) {
          copy(*children[0]);
          return;
        }

      } else {
        children.insert(children.begin(), ochild->getCopy());
      }
      continue;
    }

    if (ochild->type == NodeType::POWER) {
      mergePowerIntoProduct(*ochild);
      continue;
    }

    bool merged = false;
    for (size_t i = 0; i < children.size(); ++i) {
      auto child = children[i];
      if (child->equals(*ochild)) {
        children[i] =
            newNodeWithChildren(NodeType::POWER, {child, newConstantNode(2)});
        merged = true;
        break;
      }
    }

    if (merged)
      continue;

    children.push_back(ochild->getCopy());
  }

  if (children.size() == 1)
    copy(*children[0]);
  else if (children.empty())
    copy(*newConstantNode(1));
}

void Node::multiplyPowerWithPower(const std::shared_ptr<Node> &other) {
  if (children[0]->equals(*other->children[0])) {
    children[1]->add(other->children[1]);

    if (children[1]->isConstant(0))
      copy(*newConstantNode(1));
    else if (children[1]->isConstant(1))
      copy(*children[0]);

  } else {
    copy(*newNodeWithChildren(NodeType::PRODUCT, {getShallowCopy(), other->getCopy()}));
  }
}

void Node::multiplyProductWithPower(const std::shared_ptr<Node> &other) {
  mergePowerIntoProduct(*other);

  if (children.size() == 1)
    copy(*children[0]);
  else if (children.empty())
    copy(*newConstantNode(1));
}

void Node::multiplyProductWithOther(const std::shared_ptr<Node> &other) {
  for (size_t i = 0; i < children.size(); ++i) {
    auto child = children[i];

    if (child->equals(*other)) {
      children[i] =
          newNodeWithChildren(NodeType::POWER, {child->getShallowCopy(), newConstantNode(2)});
      return;
    }

    if (child->type == NodeType::POWER && child->children[0]->equals(*other)) {
      child->children[1]->addConstant(1);

      if (child->children[1]->isConstant(0)) {
        children.erase(children.begin() + i);
        if (children.size() == 1)
          copy(*children[0]);
      }
      return;
    }
  }

  children.push_back(other->getCopy());
}

void Node::multiplyPowerWithOther(const std::shared_ptr<Node> &other) {
  if (children[0]->equals(*other)) {
    children[1]->addConstant(1);

    if (children[1]->isConstant(0))
      copy(*newConstantNode(1));
    return;
  }

  copy(*newNodeWithChildren(NodeType::PRODUCT, {getShallowCopy(), other->getCopy()}));
}

void Node::multiplyGeneric(const std::shared_ptr<Node> &other) {
  if (equals(*other))
    copy(*newNodeWithChildren(NodeType::POWER, {getShallowCopy(), newConstantNode(2)}));
  else
    copy(*newNodeWithChildren(NodeType::PRODUCT, {getShallowCopy(), other->getCopy()}));
}

bool Node::checkExpandPower() {
  if (children[0]->type != NodeType::SUM)
    return false;

  auto expNode = children[1];
  if (expNode->type != NodeType::CONSTANT)
    return false;

  int64_t exp = static_cast<int64_t>(MBAOps::toLow64(expNode->constant, bitCount));
  if (exp > MAX_EXPONENT_TO_EXPAND)
    return false;

  expandPower(exp);
  return true;
}

void Node::expandPower(int64_t exp) {
  auto base = children[0];
  auto node = base->getCopy();

  for (int64_t i = 1; i < exp; ++i) {
    node->multiplySumWithSum(base, true);

    if (node->isConstant(0))
      break;
  }

  if (node->children.size() == 1)
    copy(*node->children[0]);
  else
    copy(*node);
}

// --------------------------------------------------------- factorize sums
bool Node::factorizeSums(bool restrictedScope) {
  if (restrictedScope)
    return checkFactorizeSum();

  bool changed = false;
  for (auto &c : children)
    if (c->factorizeSums())
      changed = true;

  if (checkFactorizeSum())
    changed = true;
  return changed;
}

bool Node::checkFactorizeSum() {
  if (type != NodeType::SUM || children.size() <= 1)
    return false;

  if (isLinear())
    return false;

  auto r = collectAllFactorsOfSum();
  auto &nodes = std::get<0>(r);
  auto &nodesToTerms = std::get<1>(r);
  auto &termsToNodes = std::get<2>(r);
  auto nodesTriviality = determineNodesTriviality(nodes);
  auto nodesOrder = determineNodesOrder(nodes);

  std::set<int> termIndices;
  for (size_t i = 0; i < children.size(); ++i)
    termIndices.insert(static_cast<int>(i));

  Batch partition({}, {}, termIndices, nodesToTerms, termsToNodes, nodesTriviality, nodesOrder);

  if (partition.isTrivial())
    return false;

  copy(*nodeFromBatch(partition, nodes, termsToNodes));
  return true;
}

std::tuple<std::vector<std::shared_ptr<Node>>,
           std::vector<std::pair<int, IndexWithMultitudeSet>>,
           std::vector<IndexWithMultitudeSet>>
Node::collectAllFactorsOfSum() {
  std::vector<std::shared_ptr<Node>> nodes;
  std::vector<std::pair<int, IndexWithMultitudeSet>> nodesToTerms;
  std::vector<IndexWithMultitudeSet> termsToNodes;

  for (size_t i = 0; i < children.size(); ++i) {
    termsToNodes.push_back({});
    auto term = children[i];
    term->collectFactors(static_cast<int>(i), 1, nodes, nodesToTerms, termsToNodes);
  }

  return {nodes, nodesToTerms, termsToNodes};
}

void Node::collectFactors(int i, int64_t multitude,
                         std::vector<std::shared_ptr<Node>> &nodes,
                         std::vector<std::pair<int, IndexWithMultitudeSet>> &nodesToTerms,
                         std::vector<IndexWithMultitudeSet> &termsToNodes) {
  if (type == NodeType::PRODUCT) {
    for (auto &factor : children)
      factor->collectFactors(i, multitude, nodes, nodesToTerms, termsToNodes);

  } else if (type == NodeType::POWER) {
    collectFactorsOfPower(i, multitude, nodes, nodesToTerms, termsToNodes);

  } else if (type != NodeType::CONSTANT) {
    checkStoreFactor(i, multitude, nodes, nodesToTerms, termsToNodes);
  }
}

void Node::collectFactorsOfPower(int i, int64_t multitude,
                                std::vector<std::shared_ptr<Node>> &nodes,
                                std::vector<std::pair<int, IndexWithMultitudeSet>> &nodesToTerms,
                                std::vector<IndexWithMultitudeSet> &termsToNodes) {
  auto base = children[0];
  auto exp = children[1];

  if (exp->type == NodeType::CONSTANT) {
    base->collectFactors(i,
                         static_cast<int64_t>(MBAOps::reduce(
                             MBAOps::toLow64(exp->constant, bitCount) *
                                 static_cast<uint64_t>(multitude), bitCount)),
                         nodes, nodesToTerms, termsToNodes);
    return;
  }

  if (exp->type == NodeType::SUM) {
    auto first = exp->children[0];
    if (first->type == NodeType::CONSTANT) {
      base->collectFactors(i,
                           static_cast<int64_t>(MBAOps::reduce(
                               MBAOps::toLow64(first->constant, bitCount) *
                                   static_cast<uint64_t>(multitude), bitCount)),
                           nodes, nodesToTerms, termsToNodes);

      auto node = getCopy();
      node->children[1]->children.erase(node->children[1]->children.begin());
      if (node->children[1]->children.size() == 1)
        node->children[1] = node->children[1]->children[0];
      node->checkStoreFactor(i, multitude, nodes, nodesToTerms, termsToNodes);
      return;
    }
  }

  checkStoreFactor(i, multitude, nodes, nodesToTerms, termsToNodes);
}

void Node::checkStoreFactor(int i, int64_t multitude,
                           std::vector<std::shared_ptr<Node>> &nodes,
                           std::vector<std::pair<int, IndexWithMultitudeSet>> &nodesToTerms,
                           std::vector<IndexWithMultitudeSet> &termsToNodes) {
  int idx = -1;
  for (size_t k = 0; k < nodes.size(); ++k)
    if (nodes[k]->equals(*this)) {
      idx = static_cast<int>(k);
      break;
    }

  if (idx == -1) {
    nodes.push_back(getShallowCopy());
    nodesToTerms.push_back({static_cast<int>(nodes.size()) - 1,
                            {IndexWithMultitude(i, multitude)}});
    termsToNodes[i].push_back(IndexWithMultitude(static_cast<int>(nodes.size()) - 1,
                                                 multitude));
    return;
  }

  auto &ntt = nodesToTerms[idx].second;
  auto res = std::find_if(ntt.begin(), ntt.end(), [i](const IndexWithMultitude &p) {
    return p.idx == i;
  });
  if (res != ntt.end())
    res->multitude += multitude;
  else
    ntt.push_back(IndexWithMultitude(i, multitude));

  auto &ttn = termsToNodes[i];
  auto res2 = std::find_if(ttn.begin(), ttn.end(), [idx](const IndexWithMultitude &p) {
    return p.idx == idx;
  });
  if (res2 != ttn.end())
    res2->multitude += multitude;
  else
    ttn.push_back(IndexWithMultitude(idx, multitude));
}

std::vector<bool> Node::determineNodesTriviality(const std::vector<std::shared_ptr<Node>> &nodes) {
  std::vector<bool> out;
  for (auto &n : nodes)
    out.push_back(n->isTrivialInFactorization());
  return out;
}

std::vector<int> Node::determineNodesOrder(const std::vector<std::shared_ptr<Node>> &nodes) {
  std::vector<std::pair<int, std::shared_ptr<Node>>> enumNodes;
  for (size_t i = 0; i < nodes.size(); ++i)
    enumNodes.push_back({static_cast<int>(i), nodes[i]});
  std::sort(enumNodes.begin(), enumNodes.end(),
            [](const auto &a, const auto &b) { return a.second->lessThan(*b.second); });
  std::vector<int> out;
  for (auto &p : enumNodes)
    out.push_back(p.first);
  return out;
}

bool Node::isTrivialInFactorization() const { return !isBitwiseBinop(); }

std::shared_ptr<Node> Node::nodeFromBatch(const Batch &batch,
                                          const std::vector<std::shared_ptr<Node>> &nodes,
                                          std::vector<IndexWithMultitudeSet> &termsToNodes) {
  auto node = newNode(NodeType::SUM);

  for (auto &c : batch.children)
    node->children.push_back(nodeFromBatch(c, nodes, termsToNodes));

  for (int a : batch.atoms) {
    reduceNodeSet(termsToNodes[a], batch.factorIndices, batch.prevFactorIndices);
    auto nodeIndices = termsToNodes[a];
    auto term = children[a];
    int64_t constant = term->getConstFactorRespectingPowers();

    if (nodeIndices.empty()) {
      node->addConstant(constant);
      continue;
    }

    if (nodeIndices.size() == 1 && constant == 1) {
      auto p = nodeIndices.back();
      nodeIndices.pop_back();
      node->children.push_back(createNodeForFactor(nodes, p));
      continue;
    }

    auto prod = newNode(NodeType::PRODUCT);
    if (constant != 1)
      prod->children.push_back(newConstantNode(constant));
    for (auto &p : nodeIndices)
      prod->children.push_back(createNodeForFactor(nodes, p));
    prod->checkResolveProductOfPowers();
    node->children.push_back(prod);
  }

  if (node->children.size() == 1)
    node->copy(*node->children[0]);

  if (batch.factorIndices.empty())
    return node;

  auto prod = newNode(NodeType::PRODUCT);
  for (auto &p : batch.factorIndices)
    prod->children.push_back(createNodeForFactor(nodes, p));
  if (node->children.size() == 1 && node->children[0]->type == NodeType::CONSTANT)
    prod->children.push_back(node);
  else
    prod->children.insert(prod->children.begin(), node);
  prod->checkResolveProductOfPowers();
  return prod;
}

void Node::reduceNodeSet(IndexWithMultitudeSet &indicesWithMultitudes,
                         const std::vector<IndexWithMultitude> &l1,
                         const std::vector<IndexWithMultitude> &l2) {
  for (auto &p : l1) {
    auto it = std::find_if(indicesWithMultitudes.begin(), indicesWithMultitudes.end(),
                           [&p](const IndexWithMultitude &q) { return q.idx == p.idx; });
    it->multitude -= p.multitude;
    if (it->multitude == 0)
      indicesWithMultitudes.erase(it);
  }
  for (auto &p : l2) {
    auto it = std::find_if(indicesWithMultitudes.begin(), indicesWithMultitudes.end(),
                           [&p](const IndexWithMultitude &q) { return q.idx == p.idx; });
    it->multitude -= p.multitude;
    if (it->multitude == 0)
      indicesWithMultitudes.erase(it);
  }
}

int64_t Node::getConstFactorRespectingPowers() const {
  if (type == NodeType::CONSTANT)
    return static_cast<int64_t>(MBAOps::toLow64(constant, bitCount));

  if (type == NodeType::PRODUCT) {
    uint64_t f = 1;
    for (auto &child : children)
      f = MBAOps::reduce(f * static_cast<uint64_t>(child->getConstFactorRespectingPowers()),
                         bitCount);
    return static_cast<int64_t>(f);
  }

  if (type != NodeType::POWER)
    return 1;

  auto base = children[0];
  if (base->type != NodeType::PRODUCT)
    return 1;
  if (base->children[0]->type != NodeType::CONSTANT)
    return 1;

  uint64_t constLow = MBAOps::toLow64(base->children[0]->constant, bitCount);
  auto exp = children[1];
  if (exp->type == NodeType::CONSTANT)
    return static_cast<int64_t>(
        MBAOps::power(constLow, MBAOps::toLow64(exp->constant, bitCount), bitCount));

  if (exp->type != NodeType::SUM)
    return 1;
  if (exp->children[0]->type != NodeType::CONSTANT)
    return 1;

  return static_cast<int64_t>(
      MBAOps::power(constLow, MBAOps::toLow64(exp->children[0]->constant, bitCount), bitCount));
}

std::shared_ptr<Node> Node::createNodeForFactor(
    const std::vector<std::shared_ptr<Node>> &nodes,
    const IndexWithMultitude &indexWithMultitude) {
  int64_t exp = indexWithMultitude.multitude;
  int idx = indexWithMultitude.idx;

  if (exp == 1)
    return nodes[idx]->getCopy();
  return newNodeWithChildren(NodeType::POWER,
                            {nodes[idx]->getCopy(), newConstantNode(exp)});
}

} // namespace MBA
} // namespace LSiMBA
