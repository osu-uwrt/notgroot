#!/usr/bin/env bash
# One-step build.
#   ./build.sh            release preset (build/release/app/notgroot)
#   ./build.sh <preset>   any configure preset in CMakePresets.json (release, dev, asan)
# NOTGROOT_JOBS overrides the parallel build jobs. Default: one per 2 GB of available memory after keeping 4 GB for
# the desktop, at most nproc.
set -euo pipefail
cd "$(dirname "$0")"

preset=${1:-release}

if [[ -r /proc/meminfo ]]; then
    available_gb=$(awk '/MemAvailable/ {print int($2 / 1048576)}' /proc/meminfo)
else
    available_gb=$(($(sysctl -n hw.memsize) / 1073741824)) # macOS: no "available" figure, use the total
fi
cores=$(getconf _NPROCESSORS_ONLN)
jobs=$(((available_gb - 4) / 2))
((jobs > cores)) && jobs=$cores
((jobs < 1)) && jobs=1
jobs=${NOTGROOT_JOBS:-$jobs}

cmake --preset "$preset"
echo "Building preset '$preset' with $jobs jobs (${available_gb} GB available; NOTGROOT_JOBS overrides)."
cmake --build --preset "$preset" -j "$jobs"

echo
echo "Built preset '$preset': run build/$preset/app/notgroot [trees directory or file]"
