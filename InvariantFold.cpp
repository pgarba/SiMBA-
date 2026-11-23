// Folding of values that don't change with a function's free inputs: see
// InvariantFold.h.
#include "InvariantFold.h"

#include <bitset>
#include <cstdint>
#include <tuple>
#include <cstdlib>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <llvm/ADT/APInt.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/PostOrderIterator.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/Analysis/ConstantFolding.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GetElementPtrTypeIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/Local.h>

using namespace llvm;

static cl::opt<bool> FoldInvariant(
    "fold-invariant", cl::Optional,
    cl::desc("Before simplifying, fold values that are the same for every "
             "sampled value of the function's free inputs (opaque predicates "
             "and constants built from, e.g., the stack pointer)"),
    cl::init(false));

static cl::opt<bool> FoldInvariantOnly(
    "fold-invariant-only", cl::Optional,
    cl::desc("With -fold-invariant: fold, and skip the MBA simplification "
             "(fast enough to run after every lifted chunk)"),
    cl::init(false));

static cl::opt<int> FoldInvariantSamples(
    "fold-invariant-samples", cl::Optional,
    cl::desc("How many input samples -fold-invariant evaluates (default 256)"),
    cl::init(256));

static cl::list<std::string> FoldInvariantArg(
    "fold-invariant-arg", cl::ZeroOrMore,
    cl::desc("NAME:ALIGN:REM -- the argument NAME is always REM modulo ALIGN "
             "(a power of two), e.g. RSP:16:8 at a function's entry"));

static cl::opt<unsigned> FoldInvariantSeed(
    "fold-invariant-seed", cl::Optional,
    cl::desc("Random seed for -fold-invariant's samples"), cl::init(0x51ce));

namespace {

// A value as the bits of its type: a scalar's own bits, a vector's lanes back
// to back (lane i at bits [i*w, (i+1)*w)), a pointer as 64 bits.
struct Shape {
  bool ok = false;
  unsigned lanes = 1;  // 1: scalar
  unsigned width = 0;  // bits per lane
  unsigned bits() const { return lanes * width; }
};

Shape shapeOf(Type *T, const DataLayout &DL) {
  Shape s;
  if (auto *IT = dyn_cast<IntegerType>(T)) {
    s.width = IT->getBitWidth();
    s.ok = s.width <= 128;
  } else if (T->isPointerTy()) {
    s.width = DL.getPointerSizeInBits(T->getPointerAddressSpace());
    s.ok = s.width == 64;
  } else if (T->isFloatTy() || T->isDoubleTy()) {
    // floating point as its bits: moved, shuffled and bitcast exactly;
    // arithmetic on it is not evaluated
    s.width = T->getPrimitiveSizeInBits().getFixedValue();
    s.ok = true;
  } else if (auto *VT = dyn_cast<FixedVectorType>(T)) {
    Type *E = VT->getElementType();
    if (E->isFloatTy() || E->isDoubleTy()) {
      s.width = E->getPrimitiveSizeInBits().getFixedValue();
      s.lanes = VT->getNumElements();
      s.ok = s.bits() <= 1024;
    } else if (auto *IT = dyn_cast<IntegerType>(E)) {
      s.width = IT->getBitWidth();
      s.lanes = VT->getNumElements();
      s.ok = s.width <= 64 && s.bits() <= 1024;
    } else if (E->isPointerTy()) {
      s.width = 64;
      s.lanes = VT->getNumElements();
      s.ok = s.bits() <= 1024;
    }
  }
  return s;
}

APInt lane(const APInt &v, const Shape &s, unsigned i) {
  return v.extractBits(s.width, i * s.width);
}

void setLane(APInt &v, const Shape &s, unsigned i, const APInt &x) {
  v.insertBits(x, i * s.width);
}

// A constant's bits, or false. Poison and undef lanes are 0: poison may be
// refined to any value, so 0 is as good as what the program would see.
bool constBits(Constant *C, const Shape &s, const DataLayout &DL, APInt &out) {
  if (!s.ok) return false;
  out = APInt(s.bits(), 0);
  if (isa<UndefValue>(C) || isa<ConstantAggregateZero>(C) ||
      isa<ConstantPointerNull>(C))
    return true;
  if (auto *CI = dyn_cast<ConstantInt>(C)) {
    if (s.lanes != 1) return false;
    out = CI->getValue();
    return out.getBitWidth() == s.bits();
  }
  if (auto *CF = dyn_cast<ConstantFP>(C)) {
    if (s.lanes != 1) return false;
    out = CF->getValueAPF().bitcastToAPInt();
    return out.getBitWidth() == s.bits();
  }
  if (auto *CE = dyn_cast<ConstantExpr>(C)) {
    // e.g. ptrtoint (inttoptr 0x401000): fold it; a global's address stays
    // unknown (it isn't a number until the loader places it)
    Constant *F = ConstantFoldConstant(CE, DL);
    if (F && F != C) return constBits(F, s, DL, out);
    if (CE->getOpcode() == Instruction::IntToPtr)
      if (auto *Op = dyn_cast<ConstantInt>(CE->getOperand(0))) {
        out = Op->getValue().zextOrTrunc(s.bits());
        return true;
      }
    return false;
  }
  if (s.lanes > 1) {
    auto *VT = dyn_cast<FixedVectorType>(C->getType());
    if (!VT) return false;
    Shape es = s;
    es.lanes = 1;
    for (unsigned i = 0; i < s.lanes; ++i) {
      Constant *E = C->getAggregateElement(i);
      if (!E) return false;
      APInt e;
      if (!constBits(E, es, DL, e)) return false;
      setLane(out, s, i, e);
    }
    return true;
  }
  return false;
}

Constant *bitsConst(Type *T, const Shape &s, const APInt &v) {
  if (auto *IT = dyn_cast<IntegerType>(T)) return ConstantInt::get(IT, v);
  auto *VT = cast<FixedVectorType>(T);
  SmallVector<Constant *, 16> els;
  for (unsigned i = 0; i < s.lanes; ++i)
    els.push_back(ConstantInt::get(VT->getElementType(), lane(v, s, i)));
  return ConstantVector::get(els);
}

// A load at a constant offset from a noalias argument (the stack, RSP) whose
// bytes were written by several earlier stores -- an XMM register stored as
// four floats, read back as one i64 -- rebuilt from those stores' bytes.
// GVN only forwards a load from one store that covers it, so such a load
// stayed a load and kept everything computed from it from folding. Walks
// back through single-predecessor blocks (a trace's trap exits are fine);
// gives up at a write it can't place (a store through another pointer, a
// call that writes memory). Little-endian targets only. Returns how many
// loads it replaced.
int forwardPartialLoads(Function &F) {
  const DataLayout &DL = F.getParent()->getDataLayout();
  if (!DL.isLittleEndian()) return 0;
  auto noaliasArg = [](Value *V) {
    auto *A = dyn_cast<Argument>(V);
    return A && A->hasNoAliasAttr();
  };
  // a stored value as an integer of its store size; nullptr: can't
  auto intable = [&](Type *T) {
    if (T->isPointerTy()) return DL.getPointerSizeInBits(T->getPointerAddressSpace()) == 64;
    if (!(T->isIntegerTy() || T->isFloatTy() || T->isDoubleTy() ||
          (T->isVectorTy() && (T->getScalarType()->isIntegerTy() || T->getScalarType()->isFloatTy() ||
                               T->getScalarType()->isDoubleTy()))))
      return false;
    if (T->isIntegerTy()) return true;  // (an i1 is zero-extended)
    return DL.getTypeSizeInBits(T).getFixedValue() == DL.getTypeStoreSize(T).getFixedValue() * 8;
  };
  std::vector<LoadInst *> loads;
  for (BasicBlock &BB : F)
    for (Instruction &I : BB)
      if (auto *LI = dyn_cast<LoadInst>(&I))
        if (LI->isSimple()) loads.push_back(LI);
  int n = 0;
  for (LoadInst *LI : loads) {
    int64_t loff = 0;
    Value *lbase = GetPointerBaseWithConstantOffset(LI->getPointerOperand(), loff, DL);
    if (!noaliasArg(lbase) || !intable(LI->getType())) continue;
    const uint64_t lsize = DL.getTypeStoreSize(LI->getType()).getFixedValue();
    if (lsize == 0 || lsize > 64) continue;
    std::vector<std::pair<Value *, int64_t>> src(lsize, {nullptr, -1});  // byte -> (stored value, its byte)
    std::vector<int> fill(lsize, -1);                                     // byte -> a memset's byte
    uint64_t remaining = lsize;
    bool fail = false;
    BasicBlock *BB = LI->getParent();
    BasicBlock::iterator it = LI->getIterator();
    SmallPtrSet<BasicBlock *, 8> seen;
    while (remaining && !fail) {
      if (it == BB->begin()) {
        BB = BB->getSinglePredecessor();
        if (!BB || !seen.insert(BB).second) {
          fail = true;
          break;
        }
        it = BB->end();
        continue;
      }
      --it;
      Instruction &X = *it;
      if (auto *SI = dyn_cast<StoreInst>(&X)) {
        if (!SI->isSimple()) {
          fail = true;
          break;
        }
        int64_t soff = 0;
        Value *sbase = GetPointerBaseWithConstantOffset(SI->getPointerOperand(), soff, DL);
        if (sbase != lbase) {
          if (noaliasArg(sbase)) continue;  // another noalias argument: apart
          fail = true;                      // anything else may overlap
          break;
        }
        Value *V = SI->getValueOperand();
        const int64_t ssize = int64_t(DL.getTypeStoreSize(V->getType()).getFixedValue());
        bool used = false;
        for (uint64_t b = 0; b < lsize; ++b) {
          const int64_t a = loff + int64_t(b);
          if (src[b].first || fill[b] >= 0 || a < soff || a >= soff + ssize) continue;
          src[b] = {V, a - soff};
          --remaining;
          used = true;
        }
        if (used && !intable(V->getType())) fail = true;
        continue;
      }
      if (auto *MS = dyn_cast<MemSetInst>(&X)) {
        int64_t soff = 0;
        Value *sbase = GetPointerBaseWithConstantOffset(MS->getDest(), soff, DL);
        if (sbase != lbase) {
          if (noaliasArg(sbase)) continue;
          fail = true;
          break;
        }
        auto *Len = dyn_cast<ConstantInt>(MS->getLength());
        auto *Val = dyn_cast<ConstantInt>(MS->getValue());
        if (!Len || !Val || MS->isVolatile()) {
          fail = true;
          break;
        }
        for (uint64_t b = 0; b < lsize; ++b) {
          const int64_t a = loff + int64_t(b);
          if (src[b].first || fill[b] >= 0 || a < soff || a >= soff + int64_t(Len->getZExtValue())) continue;
          fill[b] = int(Val->getZExtValue() & 0xff);
          --remaining;
        }
        continue;
      }
      if (auto *II = dyn_cast<IntrinsicInst>(&X)) {
        const Intrinsic::ID id = II->getIntrinsicID();
        if (id == Intrinsic::experimental_noalias_scope_decl || id == Intrinsic::trap ||
            II->isAssumeLikeIntrinsic() || II->isLifetimeStartOrEnd())
          continue;
      }
      if (X.mayWriteToMemory()) fail = true;
    }
    if (fail || remaining) continue;
    // one store of the same type at the same place: GVN's job, not ours
    if (src[0].first && src[0].second == 0 && src[0].first->getType() == LI->getType()) {
      bool one = true;
      for (uint64_t b = 1; b < lsize && one; ++b) one = src[b].first == src[0].first;
      if (one) continue;
    }
    IRBuilder<> B(LI);
    Type *IntTy = B.getIntNTy(unsigned(lsize * 8));
    auto asInt = [&](Value *V) -> Value * {
      Type *T = V->getType();
      const unsigned bits = unsigned(DL.getTypeStoreSize(T).getFixedValue() * 8);
      if (T->isPointerTy()) return B.CreatePtrToInt(V, B.getIntNTy(bits));
      if (T->isIntegerTy()) return T->getIntegerBitWidth() == bits ? V : B.CreateZExt(V, B.getIntNTy(bits));
      return B.CreateBitCast(V, B.getIntNTy(bits));
    };
    Value *acc = ConstantInt::get(IntTy, 0);
    for (uint64_t b = 0; b < lsize;) {
      if (fill[b] >= 0) {
        acc = B.CreateOr(acc, ConstantInt::get(IntTy, APInt(unsigned(lsize * 8), uint64_t(fill[b])).shl(unsigned(8 * b))));
        ++b;
        continue;
      }
      Value *V = src[b].first;
      const int64_t sb = src[b].second;
      uint64_t e = b + 1;
      while (e < lsize && src[e].first == V && src[e].second == sb + int64_t(e - b)) ++e;
      Value *VI = asInt(V);
      Value *piece = sb ? B.CreateLShr(VI, uint64_t(8 * sb)) : VI;
      const unsigned pbits = unsigned(8 * (e - b));
      if (piece->getType()->getIntegerBitWidth() != pbits) piece = B.CreateTrunc(piece, B.getIntNTy(pbits));
      if (pbits != lsize * 8) piece = B.CreateZExt(piece, IntTy);
      if (b) piece = B.CreateShl(piece, uint64_t(8 * b));
      acc = B.CreateOr(acc, piece);
      b = e;
    }
    Value *res = LI->getType()->isPointerTy() ? B.CreateIntToPtr(acc, LI->getType())
                 : LI->getType() == IntTy     ? acc
                                              : B.CreateBitCast(acc, LI->getType());
    LI->replaceAllUsesWith(res);
    LI->eraseFromParent();
    ++n;
  }
  return n;
}

class Folder {
 public:
  Folder(Function &F) : F(F), DL(F.getParent()->getDataLayout()) {}

  int run();
  int forwarded = 0;  // loads rebuilt from the stores that wrote them
  int linear = 0;     // values rebuilt as XOR-affine functions of an rsp+c value

 private:
  struct ArgSample {
    Argument *A;
    Shape s;
    uint64_t align = 1, rem = 0;
  };

  bool eval(Instruction &I, APInt &out);
  int synthesizeLinear(const std::vector<uint8_t> &state, uint8_t differs, const std::vector<int> &slot,
                       const std::vector<uint64_t> &samp, const std::vector<uint8_t> &sampOk, int samples,
                       const std::vector<std::vector<uint64_t>> &argSamp, size_t nargs);
  std::vector<Value *> argsOf;  // the sampled arguments, in argSamp's order
  bool val(Value *V, const Shape &s, APInt &out);

  Function &F;
  const DataLayout &DL;
  std::vector<Instruction *> order;
  DenseMap<Value *, unsigned> index;  // instruction -> slot in vals/known
  std::vector<Shape> shapes;
  std::vector<APInt> vals;
  std::vector<uint8_t> known;
  DenseMap<Argument *, APInt> argVals;
};

bool Folder::val(Value *V, const Shape &s, APInt &out) {
  if (auto *C = dyn_cast<Constant>(V)) return constBits(C, s, DL, out);
  if (auto *A = dyn_cast<Argument>(V)) {
    auto it = argVals.find(A);
    if (it == argVals.end()) return false;
    out = it->second;
    return out.getBitWidth() == s.bits();
  }
  auto it = index.find(V);
  if (it == index.end() || !known[it->second]) return false;
  out = vals[it->second];
  return true;
}

bool Folder::eval(Instruction &I, APInt &out) {
  const Shape s = shapes[index[&I]];
  if (!s.ok) return false;
  auto opShape = [&](Value *V) { return shapeOf(V->getType(), DL); };

  if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
    APInt a, b;
    if (!val(BO->getOperand(0), s, a) || !val(BO->getOperand(1), s, b))
      return false;
    out = APInt(s.bits(), 0);
    for (unsigned i = 0; i < s.lanes; ++i) {
      APInt x = lane(a, s, i), y = lane(b, s, i), r;
      switch (BO->getOpcode()) {
        case Instruction::Add: r = x + y; break;
        case Instruction::Sub: r = x - y; break;
        case Instruction::Mul: r = x * y; break;
        case Instruction::And: r = x & y; break;
        case Instruction::Or: r = x | y; break;
        case Instruction::Xor: r = x ^ y; break;
        case Instruction::Shl:
          if (y.uge(s.width)) return false;  // poison
          r = x.shl(y);
          break;
        case Instruction::LShr:
          if (y.uge(s.width)) return false;
          r = x.lshr(y);
          break;
        case Instruction::AShr:
          if (y.uge(s.width)) return false;
          r = x.ashr(y);
          break;
        case Instruction::UDiv:
          if (y.isZero()) return false;
          r = x.udiv(y);
          break;
        case Instruction::URem:
          if (y.isZero()) return false;
          r = x.urem(y);
          break;
        case Instruction::SDiv:
          if (y.isZero() || (x.isMinSignedValue() && y.isAllOnes())) return false;
          r = x.sdiv(y);
          break;
        case Instruction::SRem:
          if (y.isZero() || (x.isMinSignedValue() && y.isAllOnes())) return false;
          r = x.srem(y);
          break;
        default:
          return false;  // floating point
      }
      setLane(out, s, i, r);
    }
    return true;
  }

  if (auto *Cmp = dyn_cast<ICmpInst>(&I)) {
    const Shape os = opShape(Cmp->getOperand(0));
    APInt a, b;
    if (!os.ok || !val(Cmp->getOperand(0), os, a) ||
        !val(Cmp->getOperand(1), os, b))
      return false;
    out = APInt(s.bits(), 0);
    for (unsigned i = 0; i < s.lanes; ++i)
      setLane(out, s, i,
              APInt(1, ICmpInst::compare(lane(a, os, i), lane(b, os, i),
                                         Cmp->getPredicate())));
    return true;
  }

  if (auto *Sel = dyn_cast<SelectInst>(&I)) {
    const Shape cs = opShape(Sel->getCondition());
    APInt c, t, f;
    if (!cs.ok || !val(Sel->getCondition(), cs, c) ||
        !val(Sel->getTrueValue(), s, t) || !val(Sel->getFalseValue(), s, f))
      return false;
    if (cs.lanes == 1) {
      out = c.isZero() ? f : t;
      return true;
    }
    out = APInt(s.bits(), 0);
    for (unsigned i = 0; i < s.lanes; ++i)
      setLane(out, s, i, lane(c, cs, i).isZero() ? lane(f, s, i) : lane(t, s, i));
    return true;
  }

  if (auto *Cast = dyn_cast<CastInst>(&I)) {
    const Shape os = opShape(Cast->getOperand(0));
    APInt a;
    if (!os.ok || !val(Cast->getOperand(0), os, a)) return false;
    switch (Cast->getOpcode()) {
      case Instruction::BitCast:
        if (os.bits() != s.bits()) return false;
        out = a;
        return true;
      case Instruction::ZExt:
      case Instruction::SExt:
      case Instruction::Trunc:
      case Instruction::PtrToInt:
      case Instruction::IntToPtr: {
        if (os.lanes != s.lanes) return false;
        out = APInt(s.bits(), 0);
        for (unsigned i = 0; i < s.lanes; ++i) {
          APInt x = lane(a, os, i);
          setLane(out, s, i,
                  Cast->getOpcode() == Instruction::SExt ? x.sext(s.width)
                                                         : x.zextOrTrunc(s.width));
        }
        return true;
      }
      default:
        return false;  // floating point conversions
    }
  }

  if (auto *Fr = dyn_cast<FreezeInst>(&I)) return val(Fr->getOperand(0), s, out);

  if (auto *EE = dyn_cast<ExtractElementInst>(&I)) {
    const Shape vs = opShape(EE->getVectorOperand());
    const Shape is = opShape(EE->getIndexOperand());
    APInt v, ix;
    if (!vs.ok || !is.ok || !val(EE->getVectorOperand(), vs, v) ||
        !val(EE->getIndexOperand(), is, ix))
      return false;
    if (ix.uge(vs.lanes)) return false;  // poison
    out = lane(v, vs, unsigned(ix.getZExtValue()));
    return true;
  }

  if (auto *IE = dyn_cast<InsertElementInst>(&I)) {
    Shape es = s;
    es.lanes = 1;
    const Shape is = opShape(IE->getOperand(2));
    APInt v, e, ix;
    if (!is.ok || !val(IE->getOperand(0), s, v) || !val(IE->getOperand(1), es, e) ||
        !val(IE->getOperand(2), is, ix))
      return false;
    if (ix.uge(s.lanes)) return false;
    out = v;
    setLane(out, s, unsigned(ix.getZExtValue()), e);
    return true;
  }

  if (auto *SV = dyn_cast<ShuffleVectorInst>(&I)) {
    const Shape os = opShape(SV->getOperand(0));
    APInt a, b;
    if (!os.ok || !val(SV->getOperand(0), os, a) || !val(SV->getOperand(1), os, b))
      return false;
    out = APInt(s.bits(), 0);
    for (unsigned i = 0; i < s.lanes; ++i) {
      const int m = SV->getMaskValue(i);
      if (m < 0) continue;  // a poison lane: 0
      const unsigned u = unsigned(m);
      setLane(out, s, i, u < os.lanes ? lane(a, os, u) : lane(b, os, u - os.lanes));
    }
    return true;
  }

  if (auto *GEP = dyn_cast<GetElementPtrInst>(&I)) {
    if (s.lanes != 1) return false;
    APInt p;
    if (!val(GEP->getPointerOperand(), s, p)) return false;
    for (gep_type_iterator GTI = gep_type_begin(GEP), E = gep_type_end(GEP);
         GTI != E; ++GTI) {
      Value *Idx = GTI.getOperand();
      const Shape is = opShape(Idx);
      APInt ix;
      if (!is.ok || is.lanes != 1 || !val(Idx, is, ix)) return false;
      if (StructType *ST = GTI.getStructTypeOrNull()) {
        p += APInt(64, DL.getStructLayout(ST)->getElementOffset(
                           unsigned(ix.getZExtValue())));
      } else {
        const uint64_t size = GTI.getSequentialElementStride(DL);
        p += ix.sextOrTrunc(64) * APInt(64, size);
      }
    }
    out = p;
    return true;
  }

  if (auto *II = dyn_cast<IntrinsicInst>(&I)) {
    const Intrinsic::ID id = II->getIntrinsicID();
    auto arg = [&](unsigned k, APInt &a) { return val(II->getArgOperand(k), s, a); };
    APInt a, b, c;
    out = APInt(s.bits(), 0);
    switch (id) {
      case Intrinsic::ctpop:
      case Intrinsic::bswap:
      case Intrinsic::bitreverse:
      case Intrinsic::ctlz:
      case Intrinsic::cttz:
      case Intrinsic::abs:
        if (!arg(0, a)) return false;
        for (unsigned i = 0; i < s.lanes; ++i) {
          APInt x = lane(a, s, i), r;
          switch (id) {
            case Intrinsic::ctpop: r = APInt(s.width, x.popcount()); break;
            case Intrinsic::bswap:
              if (s.width % 16) return false;
              r = x.byteSwap();
              break;
            case Intrinsic::bitreverse: r = x.reverseBits(); break;
            case Intrinsic::ctlz: r = APInt(s.width, x.countl_zero()); break;
            case Intrinsic::cttz: r = APInt(s.width, x.countr_zero()); break;
            default: r = x.abs(); break;
          }
          setLane(out, s, i, r);
        }
        return true;
      case Intrinsic::umin:
      case Intrinsic::umax:
      case Intrinsic::smin:
      case Intrinsic::smax:
        if (!arg(0, a) || !arg(1, b)) return false;
        for (unsigned i = 0; i < s.lanes; ++i) {
          APInt x = lane(a, s, i), y = lane(b, s, i);
          APInt r = id == Intrinsic::umin   ? APIntOps::umin(x, y)
                    : id == Intrinsic::umax ? APIntOps::umax(x, y)
                    : id == Intrinsic::smin ? APIntOps::smin(x, y)
                                            : APIntOps::smax(x, y);
          setLane(out, s, i, r);
        }
        return true;
      case Intrinsic::fshl:
      case Intrinsic::fshr:
        if (!arg(0, a) || !arg(1, b) || !arg(2, c)) return false;
        for (unsigned i = 0; i < s.lanes; ++i) {
          APInt hi = lane(a, s, i), lo = lane(b, s, i);
          const unsigned sh = unsigned(lane(c, s, i).urem(uint64_t(s.width)));
          APInt both = hi.zext(2 * s.width).shl(s.width) | lo.zext(2 * s.width);
          APInt r = id == Intrinsic::fshl
                        ? both.shl(sh).extractBits(s.width, s.width)
                        : both.lshr(sh).trunc(s.width);
          setLane(out, s, i, r);
        }
        return true;
      default:
        return false;
    }
  }

  return false;  // loads, calls, phis, floating point: not evaluated
}

// A value that is an XOR-affine function of the bits of one "rsp + c" value L
// in its operand tree -- out = c ^ XOR over the L bits in a mask, per output
// bit -- rebuilt as c ^ XOR_d ((L >> d) & m_d). The obfuscator computes a
// byte-wise XOR by spreading bits apart (x * 0x0101.. & 0x8040.. *
// 0x0102..), adding (no carry crosses a gap) and keeping every other bit; LLVM
// sees multiplies and adds. The function is found by Gaussian elimination
// over GF(2) on the samples (all of them: more rows than unknowns, so the rest
// check it), and kept only when it is fully determined, consistent, and much
// smaller than the code it replaces.
int Folder::synthesizeLinear(const std::vector<uint8_t> &state, uint8_t differs, const std::vector<int> &slot,
                             const std::vector<uint64_t> &samp, const std::vector<uint8_t> &sampOk, int samples,
                             const std::vector<std::vector<uint64_t>> &argSamp, size_t nargs) {
  const unsigned n = unsigned(order.size());
  auto S = [&](int sl, int k) { return samp[size_t(sl) * size_t(samples) + size_t(k)]; };
  auto widthMask = [](unsigned w) { return w >= 64 ? ~0ULL : ((1ULL << w) - 1); };
  // the leaves: values that are (a pointer argument + a constant) mod 2^w,
  // one class per (argument, width, constant): the same value however many
  // instructions compute it
  struct Leaf {
    size_t arg;
    unsigned width;
    uint64_t off;
    int rep;  // an instruction computing it (for its samples)
  };
  std::vector<Leaf> leaves;
  std::map<std::tuple<size_t, unsigned, uint64_t>, int> leafClass;
  std::vector<int> leafId(n, -1);
  for (unsigned i = 0; i < n; ++i) {
    const int sl = slot[i];
    if (sl < 0 || !sampOk[size_t(sl)] || state[i] != differs) continue;
    const unsigned w = shapes[i].width;
    const uint64_t m = widthMask(w);
    for (size_t a = 0; a < nargs; ++a) {
      if (argSamp[a].empty() || argSamp[a][0] == argSamp[a][1]) continue;
      const uint64_t d0 = (S(sl, 0) - argSamp[a][0]) & m;
      bool leaf = true;
      for (int k = 1; k < samples && leaf; ++k) leaf = ((S(sl, k) - argSamp[a][size_t(k)]) & m) == d0;
      if (!leaf) continue;
      auto key = std::make_tuple(a, w, d0);
      auto it = leafClass.find(key);
      if (it == leafClass.end()) {
        if (leaves.size() >= 256) break;
        it = leafClass.emplace(key, int(leaves.size())).first;
        leaves.push_back({a, w, d0, sl});
      }
      leafId[i] = it->second;
      break;
    }
  }
  if (leaves.empty()) return 0;
  // which leaf classes each value's operand tree holds, and a size estimate
  std::vector<std::bitset<256>> cone(n);
  std::vector<double> weight(n, 1);
  for (unsigned i = 0; i < n; ++i) {
    for (Value *Op : order[i]->operands())
      if (auto *OI = dyn_cast<Instruction>(Op))
        if (auto it = index.find(OI); it != index.end() && it->second < i) {
          cone[i] |= cone[it->second];
          weight[i] = std::min(1e12, weight[i] + weight[it->second]);
        }
    if (leafId[i] >= 0) cone[i].set(size_t(leafId[i]));
  }
  // the size of a leaf's own computation, for the "smaller" test
  std::vector<double> leafWeight(leaves.size(), 1e12);
  for (unsigned i = 0; i < n; ++i)
    if (leafId[i] >= 0) leafWeight[size_t(leafId[i])] = std::min(leafWeight[size_t(leafId[i])], weight[i]);
  int replaced = 0;
  for (unsigned i = 0; i < n; ++i) {
    Instruction *I = order[i];
    const int sl = slot[i];
    if (sl < 0 || !sampOk[size_t(sl)] || state[i] != differs || leafId[i] >= 0 || I->use_empty() || cone[i].none())
      continue;
    const unsigned w = shapes[i].width;
    struct Fit {
      unsigned leaf;
      uint64_t c;
      std::vector<std::pair<int, uint64_t>> diag;  // shift d (output bit j <- L bit j+d), mask
    };
    std::optional<Fit> best;
    for (unsigned li = 0; li < leaves.size(); ++li) {
      if (!cone[i].test(li)) continue;
      const unsigned L = li;
      const int lsl = leaves[li].rep;
      const unsigned lw = leaves[li].width;
      // the bits of L that vary over the samples are the unknowns, plus the constant
      uint64_t lor = 0, land = ~0ULL;
      for (int k = 0; k < samples; ++k) { lor |= S(lsl, k); land &= S(lsl, k); }
      const uint64_t vary = (lor & ~land) & widthMask(lw);
      std::vector<int> vb;
      for (int b = 0; b < 64; ++b)
        if (vary >> b & 1) vb.push_back(b);
      const int nv = int(vb.size()) + 1;
      if (nv > 64 || samples < nv + 16) continue;
      // rows: the unknowns' bits (bit nv-1: the constant), and the value
      std::vector<std::pair<uint64_t, uint64_t>> rows(static_cast<size_t>(samples));
      for (int k = 0; k < samples; ++k) {
        uint64_t x = 0;
        for (int t = 0; t < int(vb.size()); ++t) x |= ((S(lsl, k) >> vb[size_t(t)]) & 1ULL) << t;
        x |= 1ULL << (nv - 1);
        rows[size_t(k)] = {x, S(sl, k) & widthMask(w)};
      }
      std::vector<int> pivotRow(size_t(nv), -1);
      int r = 0;
      for (int col = 0; col < nv && r < samples; ++col) {
        int p = -1;
        for (int k = r; k < samples; ++k)
          if (rows[size_t(k)].first >> col & 1) { p = k; break; }
        if (p < 0) continue;
        std::swap(rows[size_t(r)], rows[size_t(p)]);
        for (int k = 0; k < samples; ++k)
          if (k != r && (rows[size_t(k)].first >> col & 1)) {
            rows[size_t(k)].first ^= rows[size_t(r)].first;
            rows[size_t(k)].second ^= rows[size_t(r)].second;
          }
        pivotRow[size_t(col)] = r++;
      }
      if (r < nv) continue;  // not determined by the samples
      bool consistent = true;
      for (int k = r; k < samples && consistent; ++k) consistent = rows[size_t(k)].second == 0;
      if (!consistent) continue;  // not XOR-affine in L
      Fit f{L, rows[size_t(pivotRow[size_t(nv - 1)])].second, {}};
      std::map<int, uint64_t> diag;
      for (int t = 0; t < int(vb.size()); ++t) {
        const uint64_t outs = rows[size_t(pivotRow[size_t(t)])].second;  // output bits fed by L bit vb[t]
        for (unsigned j = 0; j < w; ++j)
          if (outs >> j & 1) diag[vb[size_t(t)] - int(j)] |= 1ULL << j;
      }
      f.diag.assign(diag.begin(), diag.end());
      if (!best || f.diag.size() < best->diag.size()) best = f;
    }
    if (!best || best->diag.size() > 8) continue;
    if (weight[i] - leafWeight[best->leaf] <= 4.0 * double(best->diag.size()) + 4) continue;  // no smaller
    IRBuilder<> B(I);
    // L from the argument itself (it dominates everything): (arg + off) mod 2^width
    const Leaf &lf = leaves[best->leaf];
    Value *A = argsOf[lf.arg];
    Value *L64 = A->getType()->isPointerTy() ? B.CreatePtrToInt(A, B.getInt64Ty()) : B.CreateZExtOrTrunc(A, B.getInt64Ty());
    L64 = B.CreateAdd(L64, B.getInt64(lf.off));
    if (lf.width < 64) L64 = B.CreateAnd(L64, B.getInt64(widthMask(lf.width)));
    Value *res = B.getInt64(best->c);
    for (auto [d, m] : best->diag) {
      Value *t = d >= 0 ? B.CreateLShr(L64, uint64_t(d)) : B.CreateShl(L64, uint64_t(-d));
      res = B.CreateXor(res, B.CreateAnd(t, B.getInt64(m)));
    }
    if (w < 64) res = B.CreateTrunc(res, I->getType());
    I->replaceAllUsesWith(res);
    ++replaced;
  }
  return replaced;
}

int Folder::run() {
  if (F.isDeclaration()) return 0;
  forwarded = forwardPartialLoads(F);
  // every instruction in reverse post-order, so operands come before uses
  // (control flow is ignored: each instruction is evaluated as if it ran)
  ReversePostOrderTraversal<Function *> RPOT(&F);
  for (BasicBlock *BB : RPOT)
    for (Instruction &I : *BB) {
      index[&I] = unsigned(order.size());
      order.push_back(&I);
      shapes.push_back(I.getType()->isVoidTy() ? Shape{} : shapeOf(I.getType(), DL));
    }
  const unsigned n = unsigned(order.size());

  // the free inputs: the arguments in use
  StringMap<std::pair<uint64_t, uint64_t>> constraints;
  for (const std::string &c : FoldInvariantArg) {
    SmallVector<StringRef, 3> parts;
    StringRef(c).split(parts, ':');
    uint64_t al = 1, rem = 0;
    if (parts.size() != 3 || parts[1].getAsInteger(0, al) ||
        parts[2].getAsInteger(0, rem) || al == 0 || (al & (al - 1))) {
      errs() << "[!] -fold-invariant-arg: expected NAME:ALIGN:REM, got '" << c << "'\n";
      continue;
    }
    constraints[parts[0]] = {al, rem % al};
  }
  std::vector<ArgSample> args;
  for (Argument &A : F.args()) {
    if (A.use_empty()) continue;
    ArgSample as{&A, shapeOf(A.getType(), DL)};
    if (!as.s.ok) continue;  // (left unknown: nothing using it folds)
    auto it = constraints.find(A.getName());
    if (it != constraints.end()) std::tie(as.align, as.rem) = it->second;
    args.push_back(as);
  }

  static const bool Debug = getenv("SIMBA_FOLD_DEBUG") != nullptr;
  StringMap<unsigned> unknownWhy;
  std::mt19937_64 rng(FoldInvariantSeed);
  enum : uint8_t { Unseen, Same, Differs };
  std::vector<uint8_t> state(n, Unseen);
  std::vector<APInt> first(n);
  vals.assign(n, APInt());
  known.assign(n, 0);
  const int samples = std::max(2, int(FoldInvariantSamples));
  // every scalar integer value of at most 64 bits, sample by sample, for the
  // bit-linear synthesis below; and the pointer arguments' samples
  std::vector<int> slot(n, -1);
  int slots = 0;
  for (unsigned i = 0; i < n; ++i)
    if (order[i]->getType()->isIntegerTy() && shapes[i].ok && shapes[i].width <= 64) slot[i] = slots++;
  std::vector<uint64_t> samp(size_t(slots) * size_t(samples));
  std::vector<uint8_t> sampOk(size_t(slots), 1);
  std::vector<std::vector<uint64_t>> argSamp(args.size(), std::vector<uint64_t>(size_t(samples)));
  for (int k = 0; k < samples; ++k) {
    argVals.clear();
    for (const ArgSample &as : args) {
      APInt v(as.s.bits(), 0);
      for (unsigned w = 0; w < as.s.bits(); w += 64)
        v.insertBits(APInt(std::min(64u, as.s.bits() - w), rng()), w);
      if (as.s.lanes == 1 && as.s.width == 64 && as.A->getType()->isPointerTy()) {
        // a user-space pointer: below 2^47, its low bits as constrained;
        // the first samples sweep the bits just above the alignment
        // (carries out of the constrained bits)
        uint64_t p = rng() & ((1ULL << 47) - 1);
        if (k < 64) p = (p & ~0xffffULL) | ((uint64_t(k) * 0x1010ULL) & 0xffffULL);
        p = (p & ~(as.align - 1)) | as.rem;
        v = APInt(64, p);
      } else if (as.align > 1 && as.s.lanes == 1) {
        v = (v & ~APInt(as.s.bits(), as.align - 1)) | APInt(as.s.bits(), as.rem);
      }
      argVals[as.A] = v;
      if (as.s.lanes == 1 && as.s.width == 64) argSamp[size_t(&as - args.data())][size_t(k)] = v.getZExtValue();
    }
    for (unsigned i = 0; i < n; ++i) {
      known[i] = 0;
      if (state[i] == Differs && !order[i]->hasNUsesOrMore(1)) continue;
      APInt r;
      if (!order[i]->getType()->isVoidTy() && eval(*order[i], r)) {
        vals[i] = r;
        known[i] = 1;
        if (slot[i] >= 0) samp[size_t(slot[i]) * size_t(samples) + size_t(k)] = r.getZExtValue();
        if (state[i] == Unseen) {
          state[i] = Same;
          first[i] = r;
        } else if (state[i] == Same && first[i] != r) {
          state[i] = Differs;
        }
      } else {
        state[i] = Differs;  // unknown in some sample: never folded
        if (slot[i] >= 0) sampOk[size_t(slot[i])] = 0;
        if (k == 0 && Debug) {
          // what the evaluator couldn't do: an instruction none of whose
          // operands is the reason (each one known, or a constant)
          bool operandsKnown = true;
          for (Value *Op : order[i]->operands())
            if (auto *OI = dyn_cast<Instruction>(Op))
              if (auto it = index.find(OI); it != index.end() && !known[it->second])
                operandsKnown = false;
          if (operandsKnown) {
            std::string key;
            raw_string_ostream os(key);
            os << order[i]->getOpcodeName();
            if (auto *CB = dyn_cast<CallBase>(order[i]))
              if (Function *Callee = CB->getCalledFunction()) os << " " << Callee->getName();
            os << " " << *order[i]->getType();
            ++unknownWhy[key];
          }
        }
      }
    }
  }

  // replace what came out the same every time (integers and integer vectors;
  // pointers stay -- a constant address would lose its provenance)
  if (Debug)
    for (auto &kv : unknownWhy)
      errs() << "[fold-invariant] not evaluated: " << kv.second << "x " << kv.first() << "\n";
  int folded = 0;
  for (unsigned i = 0; i < n; ++i) {
    Instruction *I = order[i];
    if (state[i] != Same || I->use_empty()) continue;
    Type *T = I->getType();
    const bool intLike = T->isIntegerTy() || (T->isVectorTy() && T->getScalarType()->isIntegerTy());
    if (!intLike) continue;
    I->replaceAllUsesWith(bitsConst(T, shapes[i], first[i]));
    ++folded;
  }
  for (const ArgSample &as : args) argsOf.push_back(as.A);
  linear = synthesizeLinear(state, Differs, slot, samp, sampOk, samples, argSamp, args.size());
  folded += linear;
  if (folded) {
    SmallVector<WeakTrackingVH, 64> dead;
    for (Instruction *I : order)
      if (isInstructionTriviallyDead(I)) dead.push_back(I);
    RecursivelyDeleteTriviallyDeadInstructionsPermissive(dead);
  }
  return folded;
}

}  // namespace

bool LSiMBA::foldInvariantsOnly() { return FoldInvariant && FoldInvariantOnly; }

int LSiMBA::foldInvariantsIfEnabled(Module &M, Function *F) {
  if (!FoldInvariant) return 0;
  int folded = 0, forwarded = 0, linear = 0;
  for (Function &Fn : M) {
    if (F && &Fn != F) continue;
    if (Fn.isDeclaration()) continue;
    Folder fo(Fn);
    folded += fo.run();
    forwarded += fo.forwarded;
    linear += fo.linear;
  }
  // (the count includes the forwarded loads: both are changes worth an opt)
  outs() << "[+] Loads forwarded from the stores that wrote them: '" << forwarded << "'\n";
  outs() << "[+] Values rebuilt as XOR-affine in an rsp+c value: '" << linear << "'\n";
  outs() << "[+] Invariant values folded: '" << folded + forwarded << "' (" << folded
         << " sampled over " << std::max(2, int(FoldInvariantSamples)) << " input values, not proved)\n";
  return folded + forwarded;
}
