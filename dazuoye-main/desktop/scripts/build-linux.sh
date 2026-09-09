#!/usr/bin/env bash
set -euo pipefail
source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
qt_root="${QT_ROOT:-$source_dir/../.runtime/qt/6.5.3/gcc_64}"
build_dir="${ELECTRA_BUILD_DIR:-$source_dir/../.runtime/qt-build}"
extra=()
if [[ -d "$source_dir/../.runtime/qt-deps/usr/include/GL" ]]; then
  extra+=("-DCMAKE_INCLUDE_PATH=$source_dir/../.runtime/qt-deps/usr/include")
fi
cmake -S "$source_dir" -B "$build_dir" -G Ninja -DCMAKE_PREFIX_PATH="$qt_root" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF "${extra[@]}"
cmake --build "$build_dir" --parallel 4
