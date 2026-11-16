#!/bin/sh
# Generates the built-in device table (devices.c) from devices.txt.
# Usage: gen-devices.sh devices.txt > devices.c

if [ $# -ne 1 ]; then
	echo "usage: $0 devices.txt" >&2
	exit 1
fi

if ! awk '/^#!format/ { v = $2; found = 1; exit } END { exit !(found && v == 1) }' "$1"; then
	echo "gen-devices.sh: $1 must declare '#!format 1'" >&2
	exit 1
fi

echo "/* Generated from devices.txt by gen-devices.sh. DO NOT EDIT. */"
echo "#include <stddef.h>"
echo "#include \"libirecovery.h\""
echo "#include \"devicedb.h\""
echo
echo "struct irecv_device irecv_devices_builtin[] = {"
awk '
/^[ \t]*$/ { body = 1; next }
/^#/ {
	if (body && $0 !~ /\*\//) { printf "\t/* %s */\n", substr($0, 3) }
	next
}
{
	name = $0
	for (i = 1; i <= 4; i++) {
		sub(/^[ \t]+/, "", name)
		f[i] = name
		sub(/[ \t].*$/, "", f[i])
		sub(/^[^ \t]+/, "", name)
	}
	sub(/^[ \t]+/, "", name)
	sub(/[ \t\r]+$/, "", name)
	if (name == "" || f[4] == "") {
		printf "gen-devices.sh: malformed line %d: %s\n", NR, $0 > "/dev/stderr"
		exit 1
	}
	if ($0 ~ /["\\]/) {
		printf "gen-devices.sh: line %d: quotes/backslashes not allowed\n", NR > "/dev/stderr"
		exit 1
	}
	printf "\t{ \"%s\", \"%s\", %s, %s, \"%s\" },\n", f[1], f[2], f[3], f[4], name
}
' "$1" || exit 1
echo "	{ NULL, NULL, -1, -1, NULL }"
echo "};"
