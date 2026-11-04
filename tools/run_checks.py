"""Run host checks; requires Python 3 and a C++17 compiler on PATH."""
from pathlib import Path
import subprocess
import sys
root = Path(__file__).resolve().parents[1]
failed = []
for test in sorted((root / 'firmware/tests').glob('check_*.py')):
    result = subprocess.run([sys.executable, str(test)], capture_output=True, text=True)
    print(f'{test.name}: {"PASS" if result.returncode == 0 else "FAIL"}', flush=True)
    if result.returncode:
        print(result.stdout + result.stderr)
        failed.append(test.name)
if failed:
    raise SystemExit(1)
