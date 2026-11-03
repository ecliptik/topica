#!/bin/bash
# Builds a release of Topica and publishes it on GitHub.
#   scripts/release.sh [--dry-run]
#
# The version is CMakeLists.txt's; HEAD must be tagged v<version> and
# CHANGELOG.md must have a dated "## <version> (YYYY-MM-DD)" section, whose
# text becomes the release notes. Builds Topica, Topica Lite and Topica Lite
# for LocalTalk (talkip, cloned at TALKIP_REF from TALKIP_REPO), each as
# .dsk, .hqx and .sit, then pushes the tag to the GitHub remote (RELEASE_REMOTE,
# else "github", else origin), creates the release there and uploads them.
# --dry-run builds and shows what it would publish.
#
# Needs: gh, with GH_TOKEN (contents write) or gh's own login; Retro68 and
# hfsutils as build.sh does; the emulators hub's sit tool for the .sit files.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
TALKIP_REPO=${TALKIP_REPO:-https://github.com/ecliptik/talkip}
TALKIP_REF=${TALKIP_REF:-v0.1.0}
dry=0
[ "${1:-}" = "--dry-run" ] && dry=1

die() { echo "release.sh: $*" >&2; exit 1; }

version=$(sed -nE 's/^project\(Topica VERSION ([0-9.]+).*/\1/p' "$here/CMakeLists.txt")
tag="v$version"
notes=$(awk -v v="$version" '
    $0 ~ "^## " v " " { on = 1; next }
    on && /^## / { exit }
    on { print }' "$here/CHANGELOG.md" | sed -e '/./,$!d')
head=$(grep -m1 -E "^## $version " "$here/CHANGELOG.md" || true)

[ -n "$head" ] || die "CHANGELOG.md has no section for $version"
if [ $dry = 0 ]; then
    echo "$head" | grep -qE '\(([0-9]{4}-[0-9]{2}-[0-9]{2})\)' || die "date $version's CHANGELOG section first: $head"
    git -C "$here" diff --quiet HEAD || die "the working tree has changes"
    git -C "$here" tag --points-at HEAD | grep -qx "$tag" || die "HEAD isn't tagged $tag"
    command -v gh >/dev/null || die "needs gh (GitHub's CLI)"
fi

echo "== building Topica $version"
"$here/scripts/build.sh" --preset full >/dev/null
"$here/scripts/build.sh" --preset lite >/dev/null
talkip=$(mktemp -d)
trap 'rm -rf "$talkip"' EXIT
git clone -q "$TALKIP_REPO" "$talkip/talkip"
git -C "$talkip/talkip" -c advice.detachedHead=false checkout -q "$TALKIP_REF"
echo "   talkip $TALKIP_REF ($(git -C "$talkip/talkip" rev-parse --short HEAD))"
TALKIP_DIR="$talkip/talkip" "$here/scripts/build.sh" --preset lite --talkip >/dev/null

display=$version
git -C "$here" tag --points-at HEAD | grep -qx "$tag" || display="$version-$(git -C "$here" rev-parse --short HEAD)"
files=()
for flavor in full lite lite-talkip; do
    for ext in dsk hqx sit; do
        f="$here/build/$flavor/Topica-$flavor-$display.$ext"
        [ -f "$f" ] && files+=("$f")
    done
done
echo "== files"
for f in "${files[@]}"; do echo "   $(basename "$f")"; done
echo "== notes"
echo "$notes" | sed 's/^/   /'
[ $dry = 1 ] && { echo "== dry run: nothing published"; exit 0; }

# GitHub: the remote's owner/repository
remote=${RELEASE_REMOTE:-}
if [ -z "$remote" ]; then
    remote=origin
    git -C "$here" remote get-url github >/dev/null 2>&1 && remote=github
fi
url=$(git -C "$here" remote get-url "$remote")
case "$url" in *github.com[:/]*) ;; *) die "remote $remote isn't on GitHub (set RELEASE_REMOTE)" ;; esac
repo=$(echo "$url" | sed -E -e 's#^.*github\.com[:/]##' -e 's#\.git$##')

gh release view "$tag" --repo "$repo" >/dev/null 2>&1 && die "GitHub already has a release for $tag"
git -C "$here" push -q "$remote" "$tag"
echo "$notes" | gh release create "$tag" --repo "$repo" --verify-tag \
    --title "Topica $version" --notes-file - "${files[@]}" >/dev/null
echo "== released: https://github.com/$repo/releases/tag/$tag"
