#!/usr/bin/env bash
# Install the pedal FX on a Move via Schwung Manager (no SSH needed).
#   scripts/install.sh [http://move.local:7700] [pedal ...] [--check]
# Build first: scripts/build.sh arm64
exec python3 "$(dirname "$0")/install.py" "$@"
