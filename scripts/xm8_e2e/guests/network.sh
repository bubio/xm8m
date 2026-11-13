#!/bin/sh
# Runs ONLY as administrator inside the dedicated E2E guest. Never invoke on Mac host.
set -eu
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
action=$1 guard=$2 platform=$3
case "$guard" in xm8-e2e-net-*) ;; *) exit 64 ;; esac
case "$guard" in *[!a-zA-Z0-9-]*) exit 64 ;; esac
[ "$(id -u)" = 0 ] || exit 77
state=/var/tmp/$guard
case "$platform:$(uname -s)" in linux:Linux|macos:Darwin) ;; *) exit 64 ;; esac
case "$action" in
arm)
    # Refuse to replace an old watchdog or another run's state.
    mkdir -m 700 "$state"
    if [ "$platform" = linux ]; then
        device=$(ip route get 1.1.1.1 | awk '{for(i=1;i<=NF;i++) if($i=="dev") {print $(i+1); exit}}')
        case "$device" in ''|lo|*[!a-zA-Z0-9_.:-]*) exit 65 ;; esac
        printf '%s\n' "$device" > "$state/interface"
    else
        device=$(route -n get default | awk '/interface:/{print $2; exit}')
        service=$(networksetup -listnetworkserviceorder | awk -v dev="$device" '
          /^\([0-9]+\)/ {name=$0; sub(/^\([0-9]+\) /,"",name)}
          index($0,"Device: " dev ")") {print name; exit}')
        [ -n "$service" ] || exit 65
        [ "$(networksetup -getnetworkserviceenabled "$service")" = Enabled ] || exit 65
        printf '%s\n' "$service" > "$state/interface"
    fi
    cp "$0" "$state/guard.sh"
    # Arm the independent rollback BEFORE disconnecting, even if the client dies.
    if [ "$platform" = linux ]; then
        systemd-run --quiet --unit="$guard" --on-active=120s /bin/sh "$state/guard.sh" restore "$guard" "$platform"
    else
        launchctl submit -l "$guard" -- /bin/sh -c 'sleep 120; exec /bin/sh "$1/guard.sh" restore "$2" "$3"' sh "$state" "$guard" "$platform"
    fi
    if [ "$platform" = linux ]; then
        ip link set dev "$device" down
    else
        networksetup -setnetworkserviceenabled "$service" off
    fi
    ;;
restore)
    [ -f "$state/interface" ] || exit 0
    device=$(cat "$state/interface")
    if [ "$platform" = linux ]; then
        ip link set dev "$device" up
        # After successful restoration, an old timer must not reconnect a later case.
        # Stop only the timer, never this restore service (it may be the watchdog).
        systemctl stop "$guard.timer"
    else
        networksetup -setnetworkserviceenabled "$device" on
    fi
    # Keep the file for this run so restoration can be retried idempotently.
    ;;
*) exit 64 ;;
esac
