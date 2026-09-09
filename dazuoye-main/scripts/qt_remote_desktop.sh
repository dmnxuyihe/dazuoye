#!/bin/bash
# Runs inside the isolated preview filesystem, never in the host desktop.
set -euo pipefail
role="$1"
if [[ "$role" == admin ]]; then display=:82; screen=1480x1020x24; port=5908; else display=:83; screen=460x900x24; port=5909; fi
export DISPLAY="$display"
mkdir -p /tmp/runtime /tmp/.X11-unix
/support/usr/bin/Xvfb "$display" -screen 0 "$screen" -nolisten tcp -ac -noreset -xkbdir /support/usr/share/X11/xkb &
xpid=$!
trap 'kill "$xpid" "${vpid:-}" "${qpid:-}" 2>/dev/null || true' EXIT
for ((i=0;i<100;i++)); do [[ -S "/tmp/.X11-unix/X${display#:}" ]] && break; sleep .1; done
/support/usr/bin/x11vnc -display "$display" -rfbport "$port" -listen 127.0.0.1 -forever -shared -nopw -nosel -noxdamage -quiet &
vpid=$!
/app/bin/electra-"$role" --api http://127.0.0.1:4173 --cache /data/cache.sqlite &
qpid=$!
wait -n "$xpid" "$vpid" "$qpid"
