"""Semantic equivalence checker: evaluates two expressions over all
combinations of their variables (GAMBA eval semantics, 2^bitCount modulus)
and prints EQUIV or DIFFERENT.

Usage: python _semcheck.py <exprA> <exprB> [bitCount]
"""
import sys
import types


class _Arr(list):
    @property
    def size(self):
        return len(self)

    def __iadd__(self, other):
        for i in range(len(self)):
            self[i] = (self[i] + other[i]) & 0xFFFFFFFFFFFFFFFF
        return self


_numpy = types.ModuleType("numpy")
_numpy.zeros = lambda n, dtype=None: _Arr([0] * n)
_numpy.uint64 = int
sys.modules["numpy"] = _numpy

sys.path.insert(0, r"C:\github\SiMBA-\external\GAMBA\src")
sys.path.insert(0, r"C:\github\SiMBA-\external\GAMBA\src\utils")
from parse import parse


def eval_all(expr, bitCount):
    m = 1 << bitCount
    tree = parse(expr, bitCount, True, False, False)
    if tree is None:
        return None
    variables = []
    tree.collect_and_enumerate_variables(variables)
    n = len(variables)
    res = []
    for i in range(1 << n):
        X = []
        t = i
        for j in range(n):
            X.append(t & 1)
            t >>= 1
        res.append(tree.eval(X) % m)
    return res


def main():
    a = sys.argv[1]
    b = sys.argv[2]
    bc = int(sys.argv[3]) if len(sys.argv) > 3 else 8
    ra = eval_all(a, bc)
    rb = eval_all(b, bc)
    if ra is None or rb is None:
        print("PARSE-FAIL")
    elif ra == rb:
        print("EQUIV")
    else:
        print("DIFFERENT")


if __name__ == "__main__":
    main()
