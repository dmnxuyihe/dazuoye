#!/usr/bin/env bash
set -euo pipefail
assignment_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
assignment_session="assignment-public"
start_public() {
  if tmux has-session -t "$assignment_session" 2>/dev/null; then
    echo "会话已存在，请使用 status 或 restart：$assignment_session"
    return
  fi
  # Migrate only this assignment's existing background port to foreground Funnel.
  assignment_ts_bin="${ASSIGNMENT_TAILSCALE_BIN:-/home/liuzihang/ljw-gf/.tailscale-liuzihang/bin/tailscale}"
  assignment_ts_socket="${ASSIGNMENT_TAILSCALE_SOCKET:-/home/liuzihang/ljw-gf/.tailscale-liuzihang/run/tailscaled.sock}"
  assignment_serve_status="$("$assignment_ts_bin" --socket="$assignment_ts_socket" serve status --json)"
  if python3 -c 'import json,sys; sys.exit(0 if "8443" in json.load(sys.stdin).get("TCP", {}) else 1)' <<< "$assignment_serve_status"; then
    "$assignment_ts_bin" --socket="$assignment_ts_socket" funnel --https=8443 off
  fi
  mkdir -p "$assignment_root/.runtime/public"
  tmux new-session -d -s "$assignment_session" -n web -c "$assignment_root" 'bash scripts/public_worker.sh web'
  tmux set-option -t "$assignment_session" remain-on-exit on
  tmux new-window -t "$assignment_session" -n funnel -c "$assignment_root" 'bash scripts/public_worker.sh funnel'
  tmux pipe-pane -o -t "$assignment_session:web" "cat >> '$assignment_root/.runtime/public/web.log'"
  tmux pipe-pane -o -t "$assignment_session:funnel" "cat >> '$assignment_root/.runtime/public/funnel.log'"
  echo "已启动独立 tmux 会话：$assignment_session（web + funnel）"
}
case "${1:-status}" in
  start) start_public ;;
  status)
    tmux list-panes -s -t "$assignment_session" -F '#{session_name}:#{window_name} pid=#{pane_pid} dead=#{pane_dead} command=#{pane_current_command}'
    curl --fail --silent http://127.0.0.1:4173/health
    echo
    ;;
  restart)
    if tmux has-session -t "$assignment_session" 2>/dev/null; then tmux kill-session -t "$assignment_session"; fi
    start_public
    ;;
  stop) tmux kill-session -t "$assignment_session" ;;
  attach) exec tmux attach-session -t "$assignment_session" ;;
  *) echo 'Usage: public_tmux.sh {start|status|restart|stop|attach}' >&2; exit 2 ;;
esac
