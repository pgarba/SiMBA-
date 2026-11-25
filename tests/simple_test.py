#!/usr/bin/env python3
import subprocess
import sys

result = subprocess.run(["echo", "hello"], capture_output=True, text=True)
print(f"Result: {result.stdout.strip()}")
print("Done!")
