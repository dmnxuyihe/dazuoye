#!/usr/bin/env bash
set -euo pipefail
assignment_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
session=assignment-qt-public
case "${1:-status}" in
 start)
   if tmux has-session -t "$session" 2>/dev/null; then echo 'Qt 公网会话已经存在'; exit 0; fi
   tmux new-session -d -s "$session" -n admin -c "$assignment_dir" 'bash scripts/qt_public_worker.sh admin'
   tmux set-option -t "$session" remain-on-exit on
   for role in user gateway funnel; do tmux new-window -t "$session" -n "$role" -c "$assignment_dir" "bash scripts/qt_public_worker.sh $role"; done
   echo 'Qt 管理端 https://lv-l40s-liuzihang.taild6df1c.ts.net/qt/?view=admin'
   echo 'Qt 用户端 https://lv-l40s-liuzihang.taild6df1c.ts.net/qt/?view=user'
   ;;
 status) tmux list-panes -s -t "$session" -F '#{window_name} dead=#{pane_dead} command=#{pane_current_command}' ;;
 stop) tmux kill-session -t "$session" ;;
 *) echo 'Usage: qt_public_tmux.sh start|status|stop'; exit 2;;
esac
