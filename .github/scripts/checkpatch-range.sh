#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Runs checkpatch.pl on each commit in <base>..<head>. A merge commit is
# checked through its remerge diff: only what it changes beyond the automatic
# merge result, such as conflict resolutions.
#
# Usage: checkpatch-range.sh <checkpatch.pl> <base> <head>
set -eu

checkpatch=$1
range=$2..$3

# Out-of-tree modules need LINUX_VERSION_CODE checks (which also trip
# CONSTANT_COMPARISON), and the repository has no MAINTAINERS file.
run_checkpatch() {
	"$checkpatch" --no-tree --no-signoff --color=always \
		--ignore LINUX_VERSION_CODE,CONSTANT_COMPARISON,FILE_PATH_CHANGES -
}

commits=$(git rev-list --reverse "$range") || {
	echo "checkpatch-range.sh: cannot list the commits in $range;" \
		"check out the full history." >&2
	exit 1
}

status=0
for c in $commits; do
	echo "::group::$(git log -1 --format='%h %s' "$c")"
	if git rev-parse -q --verify "$c^2" >/dev/null; then
		diff=$(git show --remerge-diff --format= "$c")
		if [ -n "$diff" ]; then
			printf '%s\n' "$diff" | run_checkpatch || status=1
		else
			echo "Merge commit with no changes beyond the automatic merge."
		fi
	else
		git format-patch -1 --stdout "$c" | run_checkpatch || status=1
	fi
	echo "::endgroup::"
done

if [ "$status" != 0 ]; then
	echo "::error::checkpatch reported errors or warnings; see the commit groups above."
fi
exit "$status"
