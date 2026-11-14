#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Runs checkpatch.pl on the combined diff of <base>...<head>, the changes a
# pull request makes to the code. Commits inside a pull request are working
# history and their messages are not checked; the squash commit is what
# lands on main.
#
# Usage: checkpatch-range.sh <checkpatch.pl> <base> <head>
set -eu

checkpatch=$1

diff=$(git diff "$2...$3") || {
	echo "checkpatch-range.sh: cannot diff $2...$3; check out the full" \
		"history." >&2
	exit 1
}
if [ -z "$diff" ]; then
	echo "No changes in $2...$3."
	exit 0
fi

# Out-of-tree modules need LINUX_VERSION_CODE checks (which also trip
# CONSTANT_COMPARISON), and the repository has no MAINTAINERS file.
printf '%s\n' "$diff" |
	"$checkpatch" --no-tree --no-signoff --color=always \
		--ignore LINUX_VERSION_CODE,CONSTANT_COMPARISON,FILE_PATH_CHANGES - || {
	echo "::error::checkpatch reported errors or warnings; see above."
	exit 1
}
