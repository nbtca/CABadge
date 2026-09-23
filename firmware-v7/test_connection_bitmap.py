"""Compatibility entry: the former pair-only cache is now the shared module."""
import runpy
from pathlib import Path
runpy.run_path(str(Path(__file__).with_name("test_transition_cache.py")),run_name="__main__")
