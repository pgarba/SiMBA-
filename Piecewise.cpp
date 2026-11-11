#include "Piecewise.h"

#include <vector>

namespace LSiMBA {

namespace {

// splitmix64: a fixed seed keeps the samples (and so the result) reproducible
struct SplitMix {
  uint64_t S;
  uint64_t next() {
    uint64_t Z = (S += 0x9e3779b97f4a7c15ull);
    Z = (Z ^ (Z >> 30)) * 0xbf58476d1ce4e5b9ull;
    Z = (Z ^ (Z >> 27)) * 0x94d049bb133111ebull;
    return Z ^ (Z >> 31);
  }
};

std::string join(const std::vector<std::string> &Parts, const char *Op) {
  std::string R;
  for (auto &P : Parts) {
    if (P.empty()) continue;
    if (!R.empty()) R += Op;
    R += P;
  }
  return R;
}

}  // namespace

std::string simplifyPiecewise(const std::function<uint64_t(uint64_t)> &F,
                              int Width, const std::string &V,
                              const std::vector<uint64_t> &Hints) {
  if (Width < 8 || Width > 64) return "";
  const uint64_t Mask = Width == 64 ? ~0ull : (1ull << Width) - 1;
  const uint64_t SMin = 1ull << (Width - 1);

  // The corners, then random values of every magnitude
  std::vector<uint64_t> Samples = {0,    1,        2,        3,       Mask,
                                   Mask - 1, SMin, SMin - 1, SMin + 1};
  SplitMix Rng{0x5eed};
  for (int i = 0; i < 256; i++) {
    uint64_t X = Rng.next();
    Samples.push_back(X & Mask);
    Samples.push_back((X >> (Rng.next() % Width)) & Mask);
  }
  std::vector<uint64_t> FS;
  for (auto X : Samples) FS.push_back(F(X) & Mask);

  struct Base {
    std::string S;
    uint64_t (*G)(uint64_t);
  };
  const Base Bases[] = {{"", [](uint64_t) -> uint64_t { return 0; }},
                        {V, [](uint64_t X) { return X; }},
                        {"~" + V, [](uint64_t X) { return ~X; }},
                        {"-" + V, [](uint64_t X) { return 0 - X; }}};

  for (auto &B : Bases) {
    for (const char Op : {'^', '+'}) {
      if (B.S.empty() && Op == '+') continue;  // the same as '^' on nothing
      auto Res = [&](uint64_t X, uint64_t FX) {
        return (Op == '^' ? FX ^ B.G(X) : FX - B.G(X)) & Mask;
      };
      auto R = [&](uint64_t X) { return Res(X, F(X) & Mask); };
      const char *OpS = Op == '^' ? " ^ " : " + ";

      // The residual's values on the samples: one is a constant, two may be
      // a step
      std::vector<uint64_t> Vals;
      for (size_t i = 0; i < Samples.size() && Vals.size() <= 2; i++) {
        uint64_t Y = Res(Samples[i], FS[i]);
        if (Vals.empty() || (Y != Vals[0] && (Vals.size() < 2 || Y != Vals[1])))
          Vals.push_back(Y);
      }
      if (Vals.size() == 1) {
        // One value everywhere, or everywhere but at one hinted point
        const uint64_t C0 = Vals[0];
        std::string Spike;
        bool Ok = true;
        for (auto H : Hints) {
          const uint64_t X = H & Mask, Y = R(X);
          if (Y == C0) continue;
          const std::string T = std::to_string(X);
          if ((!Spike.empty() && Spike.find("(" + V + " ^ " + T + ")") ==
                                     std::string::npos) ||
              R((X + 1) & Mask) != C0 || R((X - 1) & Mask) != C0) {
            Ok = false;
            break;
          }
          const uint64_t D = (Op == '^' ? Y ^ C0 : Y - C0) & Mask;
          Spike = "((" + V + " ^ " + T + ") <=u 0)";
          if (D != 1) Spike = "(" + Spike + " * " + std::to_string(D) + ")";
        }
        if (!Ok || (B.S.empty() && Spike.empty())) continue;
        return join({B.S, C0 ? std::to_string(C0) : "", Spike}, OpS);
      }
      if (Vals.size() != 2) continue;

      // a <= T in unsigned or signed order: search in key order, where key
      // is the value itself, or the value with the sign bit flipped
      for (const bool Signed : {false, true}) {
        const uint64_t Flip = Signed ? SMin : 0;
        auto At = [&](uint64_t Key) { return R(Key ^ Flip); };
        const uint64_t RT = At(0), RF = At(Mask);
        if (RT == RF) continue;
        uint64_t Lo = 0, Hi = Mask;
        bool Step = true;
        while (Hi - Lo > 1) {
          uint64_t Mid = Lo + (Hi - Lo) / 2, Y = At(Mid);
          if (Y == RT)
            Lo = Mid;
          else if (Y == RF)
            Hi = Mid;
          else {
            Step = false;
            break;
          }
        }
        if (!Step) continue;
        // Every sample, and the threshold's neighbours, on the right side
        auto Fits = [&](uint64_t X, uint64_t Y) {
          return Y == (((X ^ Flip) & Mask) <= Lo ? RT : RF);
        };
        for (size_t i = 0; i < Samples.size() && Step; i++)
          Step = Fits(Samples[i], Res(Samples[i], FS[i]));
        for (uint64_t K : {Lo - 1, Lo, Lo + 1, Lo + 2}) {
          const uint64_t X = (K & Mask) ^ Flip;
          Step = Step && Fits(X, R(X));
        }
        if (!Step) continue;

        const uint64_t T = Lo ^ Flip;
        const uint64_t D = (Op == '^' ? RT ^ RF : RT - RF) & Mask;
        std::string P = "(" + V + (Signed ? " <=s " : " <=u ") +
                        std::to_string(T) + ")";
        if (D != 1) P = "(" + P + " * " + std::to_string(D) + ")";
        return join({B.S, RF ? std::to_string(RF) : "", P}, OpS);
      }
    }
  }
  return "";
}

}  // namespace LSiMBA
