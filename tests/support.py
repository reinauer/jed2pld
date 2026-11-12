"""Shared paths for standalone tests, including native Windows executables."""

import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXE = Path(os.environ.get("JED2PLD", ROOT / ("jed2pld.exe" if os.name == "nt" else "jed2pld"))).resolve()
