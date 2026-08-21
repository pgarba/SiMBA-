import sys
sys.path.insert(0, r"C:\github\SiMBA-\external\GAMBA\src")
sys.path.insert(0, r"C:\github\SiMBA-\external\GAMBA\src\utils")
from simplify import simplify_linear_mba, Metric

expr = sys.argv[1]
bc = int(sys.argv[2])
try:
    r = simplify_linear_mba(expr, bc, False, False, False, True, None,
                            Metric.ALTERNATION)
    print(r)
except SystemExit:
    print("")
except Exception:
    print("<PYERR>")
