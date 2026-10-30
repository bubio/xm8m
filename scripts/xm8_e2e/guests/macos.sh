#!/bin/sh
# Guest-only, read-only readiness check. Never discard pending RA data.
set -eu
action=$1
case "$action" in
snapshot-ready)
    for name in xm8 XM8 XM8M; do
        if /usr/bin/pgrep -x "$name" >/dev/null; then exit 2; fi
    done
    for root in "$HOME/Library/Application Support/retro_pc_pi/xm8/ra" "$HOME/Library/Application Support/xm8/ra"; do
        [ ! -e "$root/pending-unlock-recovery-required" ] || exit 2
        # A missing main DB does not make orphaned recovery data safe to discard.
        for suffix in -wal -shm -journal; do
            [ ! -e "$root/library.sqlite3$suffix" ] || exit 2
        done
        if [ -e "$root/library.sqlite3" ]; then
            # A stopped, checkpointed WAL database can require sidecar creation
            # even for a normal read-only open on Apple's sqlite3. Never use
            # immutable while a WAL/rollback journal could contain pending data.
            uri=$(/usr/bin/osascript -l JavaScript -e 'ObjC.import("Foundation"); function run(a) { return ObjC.unwrap($.NSURL.fileURLWithPath(a[0]).absoluteString); }' "$root/library.sqlite3") || exit 2
            count=$(/usr/bin/sqlite3 -readonly "$uri?immutable=1" 'SELECT count(*) FROM pending_unlocks;') || exit 2
            [ "$count" = 0 ] || exit 2
        fi
    done
    echo 'snapshot-ready'
    ;;
*)
    echo 'Unknown readiness action' >&2
    exit 2
    ;;
esac
