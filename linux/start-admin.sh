#!/usr/bin/env bash
set -euo pipefail
bundle_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$bundle_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$bundle_dir/plugins"
if [[ $# -eq 0 ]]; then set -- --api https://lv-l40s-liuzihang.taild6df1c.ts.net:8443; fi
exec "$bundle_dir/bin/electra-admin" "$@"
