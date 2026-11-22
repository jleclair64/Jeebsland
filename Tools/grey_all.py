import os
d = os.path.dirname(os.path.abspath(__file__))
for s in ("stump_fix.py", "log_bark.py"):
    exec(compile(open(os.path.join(d, s)).read(), s, "exec"), {"__name__": "__main__"})
