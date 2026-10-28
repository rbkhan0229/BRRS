#!/usr/bin/env bash
set -uo pipefail
root="/Users/songchieon/Desktop/DWM3000/logs/home_ch5_aux_txtrace_20260930_152400_v2"
case_root="$1"
global_stop="/Users/songchieon/Desktop/DWM3000/logs/ubuntu_controlled/STOP_ALL"
source "$root/air-env.sh"
cd "$root/controller"
python3 study.py run --case-root "$case_root"
rc=$?
if [ "$rc" -ne 0 ]; then
  touch "$case_root/bundle/STOP" "$case_root/STOP" "$root/STOP" "$global_stop"
  python3 "$root/controller/safe_recovery.py" "$case_root" > "$case_root/POST_FAILURE_RECOVERY.console.log" 2>&1 || true
fi
exit "$rc"
