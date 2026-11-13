#!/bin/sh
# Run only through administrator execution in the dedicated macOS E2E guest.
set -eu
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
LC_ALL=C
export LC_ALL
action=${1-} guard=${2-} platform=${3-}
case "$guard" in xm8-e2e-net-?*) ;; *) exit 64 ;; esac
case "$guard" in *[!a-zA-Z0-9-]*) exit 64 ;; esac
[ "$platform" = macos ] && [ "$(uname -s)" = Darwin ] || exit 64
[ "$(id -u)" = 0 ] || exit 77
umask 077
base=/var/run/xm8-e2e-net-macos
mkdir -p -m 700 "$base"
state=$base/$guard
# Serialize the timer and explicit restoration. A retired timer must never
# reconnect a later case, even when it was already starting during cancellation.
attempt=0
until mkdir "$base/lock" 2>/dev/null; do
    attempt=$((attempt + 1))
    [ "$attempt" -lt 50 ] || exit 75
    sleep 0.1
done
trap 'rmdir "$base/lock"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
case "$action" in
arm)
    expected_device=${4-} expected_service=${5-}
    case "$expected_device" in en[0-9]*) ;; *) exit 64 ;; esac
    case "$expected_device" in *[!a-zA-Z0-9]*) exit 64 ;; esac
    [ -n "$expected_service" ] || exit 64
    [ ! -f "$base/active" ] || exit 65
    device=$(route -n get default | awk '/interface:/{print $2; exit}')
    [ "$device" = "$expected_device" ] || exit 65
    service=$(networksetup -listnetworkserviceorder | awk -v dev="$device" '
      /^\([0-9]+\)/ {name=$0; sub(/^\([0-9]+\) /,"",name)}
      index($0,"Device: " dev ")") {print name; exit}')
    [ "$service" = "$expected_service" ] || exit 65
    [ "$(networksetup -getnetworkserviceenabled "$service")" = Enabled ] || exit 65
    # This root-owned copy keeps restoration independent of later file transfer.
    mkdir -m 700 "$state"
    printf '%s\n' "$device" > "$state/device"
    printf '%s\n' "$service" > "$state/service"
    cp "$0" "$state/guard.sh"
    printf '%s\n' "$guard" > "$base/active"
    launchctl submit -l "$guard" -- /bin/sh -c 'sleep 120; exec /bin/sh "$1/guard.sh" watchdog "$2" macos' sh "$state" "$guard"
    launchctl list "$guard" >/dev/null
    networksetup -setnetworkserviceenabled "$service" off
    [ "$(networksetup -getnetworkserviceenabled "$service")" = Disabled ] || exit 1
    printf '{"guard":"%s","state":"down","verified":true,"adapter_count":1,"watchdog_seconds":120,"device":"%s"}\n' "$guard" "$device"
    ;;
restore|watchdog)
    [ -f "$state/service" ] && [ -f "$state/device" ] || exit 2
    service=$(cat "$state/service") device=$(cat "$state/device")
    active=$(cat "$base/active" 2>/dev/null || true)
    if [ "$active" != "$guard" ]; then
        # Watchdog jobs are one-shot. A cancelled/old timer is a no-op.
        [ "$action" != watchdog ] || exit 0
        [ -z "$active" ] || exit 2
    else
        networksetup -setnetworkserviceenabled "$service" on
    fi
    [ "$(networksetup -getnetworkserviceenabled "$service")" = Enabled ] || exit 1
    if [ "$active" = "$guard" ]; then rm "$base/active"; fi
    # Never remove the watchdog from inside itself: launchctl can terminate it.
    # It is one-shot; a later explicit restore removes even its completed job.
    if [ "$action" = restore ]; then
        if launchctl list "$guard" >/dev/null 2>&1; then
            launchctl remove "$guard"
        fi
        if launchctl list "$guard" >/dev/null 2>&1; then exit 1; fi
    fi
    printf '%s\n' "$action" > "$state/restored"
    printf '{"guard":"%s","state":"up","verified":true,"adapter_count":1,"device":"%s"}\n' "$guard" "$device"
    ;;
*) exit 64 ;;
esac
