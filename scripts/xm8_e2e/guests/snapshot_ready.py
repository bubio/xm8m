#!/usr/bin/env python3
"""Read counts only. Never discard a pending RA unlock by snapshot rollback."""
from contextlib import closing
from pathlib import Path
import sqlite3
import sys


def safe_to_restore(roots):
    for process in Path('/proc').glob('[0-9]*/comm'):
        try:
            if process.read_text().strip() == 'xm8':
                return False
        except (FileNotFoundError, PermissionError):
            continue
    for root in map(Path, roots):
        if (root / 'pending-unlock-recovery-required').exists():
            return False
        database = root / 'library.sqlite3'
        if not database.exists():
            continue
        try:
            with closing(sqlite3.connect(database.resolve().as_uri() + '?mode=ro', uri=True)) as db:
                if db.execute('SELECT count(*) FROM pending_unlocks').fetchone()[0]:
                    return False
        except sqlite3.Error:
            return False
    return True


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(2)
    sys.exit(0 if safe_to_restore(sys.argv[1:]) else 2)
