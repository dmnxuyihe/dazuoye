#!/usr/bin/env bash
set -euo pipefail
assignment_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$assignment_dir"
runtime="$assignment_dir/.runtime/qt-public"
case "${1:-}" in
  admin|user)
    role="$1"
    mkdir -p "$runtime/$role"
    exec "$runtime/root/usr/bin/bwrap" --unshare-all --share-net --die-with-parent --new-session --clearenv \
      --ro-bind /usr/lib /usr/lib --ro-bind /lib /lib --ro-bind /lib64 /lib64 \
      --ro-bind "$runtime/root/usr/share/X11/xkb" /usr/share/X11/xkb \
      --ro-bind /usr/share/fonts /usr/share/fonts \
      --ro-bind /etc/fonts /etc/fonts --ro-bind /etc/ssl /etc/ssl \
      --ro-bind /etc/resolv.conf /etc/resolv.conf \
      --ro-bind /bin/bash /bin/bash --symlink bash /bin/sh --ro-bind /bin/sleep /bin/sleep --ro-bind /bin/mkdir /bin/mkdir \
      --ro-bind "$runtime/root/usr/bin/xkbcomp" /usr/bin/xkbcomp \
      --ro-bind "$assignment_dir/deliverables/qt/linux" /app \
      --ro-bind "$runtime/root" /support \
      --ro-bind "$assignment_dir/scripts/qt_remote_desktop.sh" /launch.sh \
      --bind "$runtime/$role" /data --tmpfs /tmp --proc /proc --dev /dev \
      --dir /home/guest --dir /run --chdir /data \
      --setenv LANG C.UTF-8 --setenv PATH /bin --setenv LD_LIBRARY_PATH /app/lib:/support/usr/lib/x86_64-linux-gnu \
      --setenv QT_PLUGIN_PATH /app/plugins --setenv QT_QPA_PLATFORM xcb \
      --setenv XDG_RUNTIME_DIR /tmp/runtime --setenv XDG_CONFIG_HOME /data/config \
      --setenv XDG_CACHE_HOME /data/web-cache --setenv XDG_DATA_HOME /data/web-data \
      --setenv QTWEBENGINE_CHROMIUM_FLAGS --disable-gpu \
      --setenv XKB_CONFIG_ROOT /support/usr/share/X11/xkb --setenv XKB_BINDIR /support/usr/bin \
      /bin/bash /launch.sh "$role"
    ;;
  gateway)
    export PYTHONPATH="$runtime/python"
    exec .venv/bin/python -m websockify --web "$runtime/web" --token-plugin TokenFile --token-source "$runtime/targets.conf" 127.0.0.1:6082
    ;;
  funnel)
    ts_bin=/home/liuzihang/ljw-gf/.tailscale-liuzihang/bin/tailscale
    ts_socket=/home/liuzihang/ljw-gf/.tailscale-liuzihang/run/tailscaled.sock
    "$ts_bin" --socket="$ts_socket" funnel --bg --https=443 --set-path=/qt http://127.0.0.1:6082
    # Port 443 is shared with pre-existing routes. Own only the /qt mapping.
    cleanup() { "$ts_bin" --socket="$ts_socket" funnel --https=443 --set-path=/qt off; }
    trap cleanup EXIT
    trap 'exit 0' HUP INT TERM
    while sleep 3600; do :; done
    ;;
  *) exit 2;;
esac
