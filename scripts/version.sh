#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Prints the package version, taken from the release tags: X.Y.Z on the tag
# vX.Y.Z, X.Y.Z-N-gHASH N commits after it, 0-gHASH without a tag. Only tags
# of exactly that form count; the release workflow allows one per commit.
#
# A git clone uses git describe. An archive uses the VERSION file, which git
# archive fills in through export-subst (see .gitattributes). GitHub's source
# downloads leave %(describe) empty but list the tag among the refs (%D) when
# the download is of a tag.
set -eu

cd "$(dirname "$0")/.."

if [ -e .git ] && ! err=$(git rev-parse --show-toplevel 2>&1 >/dev/null); then
	echo "version.sh: git cannot read this clone: $err" >&2
	exit 1
fi

if [ "$(git rev-parse --show-toplevel 2>/dev/null)" = "$(pwd -P)" ]; then
	if version=$(git describe --tags --match 'v[0-9]*' --exclude 'v*[!0-9.]*' \
		--dirty 2>/dev/null); then
		echo "${version#v}"
	else
		echo "0-g$(git rev-parse --short HEAD)"
	fi
	exit 0
fi

# Command substitution drops the final newline, present or not.
line=$(tr -d '\r' <VERSION)
IFS='|' read -r described refs hash <<EOF
$line
EOF
case $described in
\$Format:*)
	echo "version.sh: no git history and VERSION is not filled in;" \
		"build from a git clone or a release tarball." >&2
	exit 1
	;;
esac

if [ -n "$described" ]; then
	echo "${described#v}"
	exit 0
fi

tag=$(echo "$refs" | tr ',' '\n' |
	sed -n 's/^ *tag: v\([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\)$/\1/p' | head -n 1)
if [ -n "$tag" ]; then
	echo "$tag"
else
	echo "0-g$hash"
fi
