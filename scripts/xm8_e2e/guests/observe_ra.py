#!/usr/bin/env python3
"""Read aggregate outbox counts only; never output accounts, IDs or errors."""
from contextlib import closing
from pathlib import Path
import json
import sqlite3
import sys


def observe(database):
    path = Path(database).resolve()
    if not path.is_file():
        raise ValueError("RA database unavailable")
    with closing(sqlite3.connect(path.as_uri() + "?mode=ro", uri=True)) as db:
        rows = db.execute("SELECT status, count(*) FROM pending_unlocks GROUP BY status").fetchall()
    if any(status not in (0, 1) for status, _ in rows):
        raise ValueError("Unknown outbox status")
    counts = dict(rows)
    return {"pending": counts.get(0, 0), "held": counts.get(1, 0)}


if __name__ == "__main__":
    try:
        print(json.dumps(observe(sys.argv[1])))
    except (IndexError, ValueError, sqlite3.Error):
        print("RA aggregate observation unavailable", file=sys.stderr)
        sys.exit(2)
