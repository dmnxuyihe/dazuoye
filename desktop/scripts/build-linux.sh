#!/usr/bin/env bash
set -euo pipefail
source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
qt_root="${QT_ROOT:-$source_dir/../.runtime/qt/6.5.3/gcc_64}"
deps_root="$source_dir/../.runtime/qt-deps/usr"
# Also allow a system Qt installation or a caller-supplied Qt SDK.
prefix_path="${CMAKE_PREFIX_PATH:-}"
if [[ -d "$qt_root" ]]; then prefix_path="$qt_root${prefix_path:+;$prefix_path}"; fi
if [[ -d "$deps_root" ]]; then
  prefix_path="$deps_root${prefix_path:+;$prefix_path}"
  export LD_LIBRARY_PATH="$deps_root/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
build_dir="${ELECTRA_BUILD_DIR:-$source_dir/../.runtime/qt-build}"
extra=()
if [[ -d "$source_dir/../.runtime/qt-deps/usr/include/GL" ]]; then
  extra+=("-DCMAKE_INCLUDE_PATH=$source_dir/../.runtime/qt-deps/usr/include")
fi
cmake -S "$source_dir" -B "$build_dir" -G Ninja -DCMAKE_PREFIX_PATH="$prefix_path" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING="${BUILD_TESTING:-OFF}" "${extra[@]}"
cmake --build "$build_dir" --parallel "${BUILD_JOBS:-4}"
