#!/usr/bin/env bash

set -euo pipefail

cat >&2 <<'EOF'
ERROR: brrs_exp3_flash_and_log.sh is deprecated.

It used separate JLinkExe and JLinkRTTLogger connections and could lose the
1,000-row EXTTXE dump. Use the single-connection PyLink capture instead:

  ./brrs_exp3_capture.sh tx A 1 iron_door_nlos 6.9
  ./brrs_exp3_capture.sh rx A 1 iron_door_nlos 6.9
EOF
exit 2
