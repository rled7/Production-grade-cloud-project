"""Test fixtures and path setup."""
from __future__ import annotations

import os
import sys

# Ensure the project root is on sys.path so `app` package imports work
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)
