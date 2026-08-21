import sys
import types

# numpy is not installed in this environment; provide a minimal stub before
# importing simplify_general (which does `import numpy as np`).
#
# The stub must mimic the small subset of numpy used by simplify_general:
#   * np.zeros(n, dtype=np.uint64) -> a zero array of length n
#   * arr += other                 -> element-wise addition (numpy semantics),
#                                     NOT list concatenation
#   * arr.size                     -> length
#   * np.uint64                    -> a dtype placeholder
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

# The sandbox blocks named pipes, so multiprocessing.Manager()/Process()
# (used by GeneralSimplifier.simplify for its 30s timeout) fail with
# PermissionError. Patch them with inline fakes: the "process" runs the
# target synchronously; the surrounding 30s subprocess timeout in the
# differential test plays the role of the original join(30) timeout.
import multiprocessing


class _FakeDict(dict):
    # multiprocessing's SyncDict proxy returns a list from values();
    # mimic that (simplify_general does returnDict.values()[0]).
    def values(self):
        return list(dict.values(self))


class _FakeManager:
    def dict(self):
        return _FakeDict()


class _FakeProcess:
    def __init__(self, target=None, args=()):
        self._target = target
        self._args = args

    def start(self):
        self._target(*self._args)

    def join(self, timeout=None):
        pass

    def is_alive(self):
        return False

    def kill(self):
        pass


multiprocessing.Manager = _FakeManager
multiprocessing.Process = _FakeProcess

sys.path.insert(0, r"C:\github\SiMBA-\external\GAMBA\src")
from simplify_general import simplify_mba

expr = sys.argv[1]
bc = int(sys.argv[2])
try:
    r = simplify_mba(expr, bc, False, False, None)
    print(r)
except SystemExit:
    print("")
except Exception:
    print("<PYERR>")
