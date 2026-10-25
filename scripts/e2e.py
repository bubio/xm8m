#!/usr/bin/env python3
"""Local XM8M GUI E2E; never called by CI or release workflows."""
from xm8_e2e.runner import main

if __name__ == "__main__":
    raise SystemExit(main())
