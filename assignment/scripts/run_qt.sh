#!/usr/bin/env bash
set -euo pipefail
assignment_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
role="${1:-admin}"
if [[ $# -gt 0 ]]; then shift; fi
case "$role" in admin|user) ;; *) echo '用法: scripts/run_qt.sh admin|user [--api URL]' >&2; exit 2;; esac
export QT_PLUGIN_PATH="$assignment_dir/.runtime/qt/6.5.3/gcc_64/plugins"
export LD_LIBRARY_PATH="$assignment_dir/.runtime/qt/6.5.3/gcc_64/lib:$assignment_dir/.runtime/qt-deps/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$assignment_dir/.runtime/qt-build/electra-$role" --cache "$assignment_dir/.runtime/qt-$role.sqlite" "$@"
