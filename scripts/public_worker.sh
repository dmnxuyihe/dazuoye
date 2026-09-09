#!/usr/bin/env bash
set -euo pipefail
assignment_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$assignment_root"
case "${1:-web}" in
  web)
    bash scripts/local_postgres14.sh start
    exec .venv/bin/charging-core serve --host 127.0.0.1 --port 4173
    ;;
  funnel)
    assignment_ts_bin="${ASSIGNMENT_TAILSCALE_BIN:-/home/liuzihang/ljw-gf/.tailscale-liuzihang/bin/tailscale}"
    assignment_ts_socket="${ASSIGNMENT_TAILSCALE_SOCKET:-/home/liuzihang/ljw-gf/.tailscale-liuzihang/run/tailscaled.sock}"
    exec "$assignment_ts_bin" --socket="$assignment_ts_socket" funnel --yes --https=8443 http://127.0.0.1:4173
    ;;
  *) echo 'Usage: public_worker.sh web|funnel' >&2; exit 2 ;;
esac
