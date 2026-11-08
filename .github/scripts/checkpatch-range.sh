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
# CONSTANT_COMPARISON), and the repository has no MAINTAINERS file. It also
# lacks the history of mainline and other upstreams, so checkpatch cannot
# resolve commits cited from them (UNKNOWN_COMMIT_ID); check_fixes() takes
# over that check for Fixes: tags, which name commits in this repository.
ignore=LINUX_VERSION_CODE,CONSTANT_COMPARISON,FILE_PATH_CHANGES
ignore=$ignore,UNKNOWN_COMMIT_ID
run_checkpatch() {
	"$checkpatch" --no-tree --no-signoff --color=always --ignore "$ignore" -
}

# Prints each commit ID in a Fixes: tag of commit $1 that this repository
# lacks. A tag is what checkpatch's BAD_FIXES_TAG check takes as one:
# /^\s*fixes:?\s*(?:commit\s*)?([0-9a-f]{5,40})/i.
check_fixes() {
	git log -1 --format=%B "$1" |
		awk '{
			l = tolower($0)
			tag = "^[[:space:]]*fixes:?[[:space:]]*" \
				"(commit[[:space:]]*)?[0-9a-f]+"
			if (!match(l, tag))
				next
			id = substr(l, 1, RLENGTH)
			sub(/^.*[^0-9a-f]/, "", id)
			if (length(id) >= 5)
				print substr(id, 1, 40)
		}' |
		while read -r id; do
			git rev-parse -q --verify "$id^{commit}" >/dev/null ||
				printf '%s\n' "$id"
		done
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
	for id in $(check_fixes "$c"); do
		echo "::error::Fixes: $id is not a commit in this repository (or is" \
			"ambiguous); copy the ID from git log --abbrev=12."
		status=1
	done
	echo "::endgroup::"
done

if [ "$status" != 0 ]; then
	echo "::error::checkpatch or the Fixes: check failed; see the commit groups above."
fi
exit "$status"
