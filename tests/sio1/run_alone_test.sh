#!/bin/sh
# One instance of the core with nothing at the other end of its port, running
# psx/linktest.exe as role 5. A game has to find the same DSR and CTS levels
# with the link cable option off and with it on while the other console is
# not there: those of a port without a cable.
# usage: run_alone_test.sh <core.so> <linktest.exe> [drc]
core=$1; exe=$2; drc=${3:-enabled}
host=${RETRO_HOST_BIN:-./retro_host}
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
export PCSX_SIO1_LINK_DIR="$dir" HOST_DIR="$dir" PCSX_SIO1_LINK_NAME="alone$$"
fail=0
# the option off is the old fixed SIO_STAT, on is the emulated port
for t in "disabled 1a5" "console A 185" "console B 185"; do
	link=${t% *}; want=${t##* }
	"$host" "$core" "$exe" 5 "$link" exact "$drc" 300 > "$dir/out" 2> "$dir/err"
	rc=$?
	got=$(sed -n 's/^alone sr //p' "$dir/out")
	if [ $rc -eq 0 ] && [ "$got" = "$want" ] \
	   && grep -q "status 1 count 1 " "$dir/out"; then
		echo "link cable $link: sr $got, a byte leaves the port"
	else
		echo "link cable $link: FAILED (sr '$got', wanted $want)"
		cat "$dir/out"; sed 's/^/    /' "$dir/err"
		fail=1
	fi
done
exit $fail
