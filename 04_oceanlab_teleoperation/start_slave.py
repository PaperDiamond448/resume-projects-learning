from pathlib import Path
import os, subprocess, sys
root = Path(__file__).resolve().parent / "python"
raise SystemExit(subprocess.call([sys.executable, "-m", "client.client_app", *sys.argv[1:]], cwd=root))
