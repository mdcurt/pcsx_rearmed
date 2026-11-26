#!/bin/sh
# Two instances of the core, linked, each running psx/linktest.exe.
# usage: run_emu_test.sh <core.so> <linktest.exe> [timing] [drc] [jitter seed]
# LATE=<seconds> starts console A that much after console B, which by then
# is reading its port over and over.
core=$1; exe=$2; timing=${3:-exact}; drc=${4:-enabled}; jit=$5
host=${RETRO_HOST_BIN:-./retro_host}
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
export PCSX_SIO1_LINK_DIR="$dir" HOST_DIR="$dir" PCSX_SIO1_LINK_NAME="emu$$"
[ -n "$jit" ] && export HOST_JITTER="$jit"
"$host" "$core" "$exe" 2 "console B" "$timing" "$drc" 3000 > "$dir/b" 2> "$dir/berr" &
pid=$!
[ -n "$LATE" ] && sleep "$LATE"
"$host" "$core" "$exe" 1 "console A" "$timing" "$drc" 3000 > "$dir/a" 2> "$dir/aerr"
ra=$?
wait $pid
rb=$?
grep -h "^role\|^poll\|^irq" "$dir/a" "$dir/b"
grep -h "sio1\|frames in\|savestate" "$dir/aerr" "$dir/berr" | sed 's/^/    /'
[ $ra -eq 0 ] && [ $rb -eq 0 ]
