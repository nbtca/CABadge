"""Compatibility entrypoint used by frozen historical instructions."""
from pathlib import Path
import runpy

runpy.run_path(str(Path(__file__).resolve().parents[1] / 'tools/repository/check_repository.py'), run_name='__main__')
