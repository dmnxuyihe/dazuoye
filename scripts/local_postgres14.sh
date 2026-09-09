#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
runtime_dir="$project_dir/.runtime/postgresql14"
pg_root="$runtime_dir/root"
data_dir="$runtime_dir/data"
socket_dir="$runtime_dir/socket"
log_file="$runtime_dir/postgresql.log"
pg_bin="$pg_root/usr/lib/postgresql/14/bin"
pg_lib="$pg_root/usr/lib/x86_64-linux-gnu:$pg_root/usr/lib/postgresql/14/lib"
port="${CHARGING_LOCAL_PG_PORT:-55432}"

bootstrap() {
  if [[ -x "$pg_bin/postgres" ]]; then return; fi
  command -v apt-get >/dev/null || { echo "需要 apt-get 下载 PostgreSQL 14 运行包" >&2; exit 1; }
  command -v dpkg-deb >/dev/null || { echo "需要 dpkg-deb 解包 PostgreSQL 14" >&2; exit 1; }
  mkdir -p "$pg_root"
  download_dir="$(mktemp -d)"
  trap 'rm -rf "$download_dir"' RETURN
  (
    cd "$download_dir"
    apt-get download postgresql-14 postgresql-client-14 libpq5 >/dev/null
    for package in ./*.deb; do dpkg-deb -x "$package" "$pg_root"; done
  )
}

pg() { LD_LIBRARY_PATH="$pg_lib" "$pg_bin/$@"; }

start() {
  bootstrap
  mkdir -p "$runtime_dir" "$socket_dir"
  if [[ ! -f "$data_dir/PG_VERSION" ]]; then
    mkdir -p "$data_dir"
    pg initdb -D "$data_dir" -A trust -U postgres --no-locale --encoding=UTF8 >/dev/null
  fi
  if pg pg_ctl -D "$data_dir" status >/dev/null 2>&1; then
    echo "PostgreSQL 14 已运行：127.0.0.1:$port"
    return
  fi
  pg pg_ctl -D "$data_dir" -l "$log_file" -o "-h 127.0.0.1 -p $port -k $socket_dir" start >/dev/null
  echo "PostgreSQL 14 已启动：127.0.0.1:$port"
}

case "${1:-start}" in
  bootstrap) bootstrap; echo "PostgreSQL 14 运行时已准备" ;;
  start) start ;;
  init) start; (cd "$project_dir" && .venv/bin/charging-core init-db) ;;
  stop)
    bootstrap
    if pg pg_ctl -D "$data_dir" status >/dev/null 2>&1; then pg pg_ctl -D "$data_dir" stop -m fast >/dev/null; fi
    echo "PostgreSQL 14 已停止"
    ;;
  status)
    bootstrap
    pg pg_ctl -D "$data_dir" status
    ;;
  *) echo "用法：$0 {bootstrap|start|init|stop|status}" >&2; exit 2 ;;
esac
