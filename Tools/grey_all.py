import os
d = os.path.dirname(os.path.abspath(__file__))
exec(compile(open(os.path.join(d, "log_bark2.py")).read(), "log_bark2.py", "exec"), {"__name__": "__main__"})
