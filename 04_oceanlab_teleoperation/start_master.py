from pathlib import Path
import os, subprocess, sys
root = Path(__file__).resolve().parent / "python"
raise SystemExit(subprocess.call([sys.executable, str(root / "main.py"), *sys.argv[1:]], cwd=root))
