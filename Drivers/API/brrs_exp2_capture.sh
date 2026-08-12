#!/usr/bin/env bash

# Stable entry point retained for the experiment guide and older commands.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "${SCRIPT_DIR}/brrs_exp2_capture_v3.sh" "$@"
