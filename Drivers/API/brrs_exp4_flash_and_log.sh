#!/usr/bin/env bash

set -euo pipefail

cat >&2 <<'EOF'
This legacy dual-J-Link script is disabled because a separate logger connection
can miss the RTT control block or lose the start/end of an Experiment 4 run.

Use the single-PyLink build/flash/capture/verify command instead:
  Drivers/API/brrs_exp4_capture.sh <init|N2..N8> <preamble> <sensor-count> <run> <environment> [distance]

See Drivers/API/BRRS_EXPERIMENT4_AUTO_CAPTURE_KR.md.
EOF
exit 2
