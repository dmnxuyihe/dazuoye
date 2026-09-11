#!/usr/bin/env bash
set -euo pipefail
assignment_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
role="${1:-admin}"
if [[ $# -gt 0 ]]; then shift; fi
case "$role" in admin|user) ;; *) echo '用法: scripts/run_qt.sh admin|user [--api URL]' >&2; exit 2;; esac
qt_root="${QT_ROOT:-$assignment_dir/.runtime/qt/6.5.3/gcc_64}"
build_dir="${ELECTRA_BUILD_DIR:-$assignment_dir/.runtime/qt-build}"
if [[ -d "$qt_root/plugins" ]]; then export QT_PLUGIN_PATH="$qt_root/plugins"; fi
if [[ -d "$qt_root/lib" ]]; then
  export LD_LIBRARY_PATH="$qt_root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
if [[ -d "$assignment_dir/.runtime/qt-deps/usr/lib/x86_64-linux-gnu" ]]; then
  export LD_LIBRARY_PATH="$assignment_dir/.runtime/qt-deps/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
if [[ ! -x "$build_dir/electra-$role" ]]; then
  echo "缺少客户端：$build_dir/electra-$role；请先运行 bash desktop/scripts/build-linux.sh" >&2
  exit 1
fi
exec "$build_dir/electra-$role" --cache "$assignment_dir/.runtime/qt-$role.sqlite" "$@"
