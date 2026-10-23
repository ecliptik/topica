#!/bin/bash
# Builds a release of Topica and publishes it on Forgejo.
#   scripts/release.sh [--dry-run]
#
# The version is CMakeLists.txt's; HEAD must be tagged v<version> and
# CHANGELOG.md must have a dated "## <version> (YYYY-MM-DD)" section, whose
# text becomes the release notes. Builds Topica, Topica Lite and Topica Lite
# for LocalTalk (talkip, cloned at TALKIP_REF from TALKIP_REPO), each as
# .dsk, .hqx and .sit, then creates the release on the repository's Forgejo
# (from origin) and uploads them. --dry-run builds and shows what it would
# publish.
#
# Needs: FORGEJO_TOKEN (repository write), curl, jq; Retro68 and hfsutils
# as build.sh does; the emulators hub's sit tool for the .sit files.
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
    [ -n "${FORGEJO_TOKEN:-}" ] || die "set FORGEJO_TOKEN"
    command -v jq >/dev/null || die "needs jq"
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

# Forgejo, from origin: its host and owner/repository (a private host stays out of this file)
origin=$(git -C "$here" remote get-url origin)
host=$(echo "$origin" | sed -E -e 's#^[a-z+]+://([^@/]+@)?([^/:]+).*#\2#' -e 's#^[^@/]+@([^:/]+):.*#\1#')
repo=$(echo "$origin" | sed -E -e 's#^[^:]+:(//[^/]+/)?##' -e 's#\.git$##')
api="https://$host/api/v1/repos/$repo"
auth=(-H "Authorization: token $FORGEJO_TOKEN")

git -C "$here" push -q origin "$tag"
[ "$(curl -s -o /dev/null -w '%{http_code}' "${auth[@]}" "$api/releases/tags/$tag")" = 200 ] \
    && die "Forgejo already has a release for $tag"
id=$(curl -s -X POST "${auth[@]}" -H "Content-Type: application/json" "$api/releases" \
    -d "$(jq -n --arg t "$tag" --arg n "Topica $version" --arg b "$notes" \
        '{tag_name: $t, name: $n, body: $b, draft: false, prerelease: false}')" | jq -r '.id // empty')
[ -n "$id" ] || die "Forgejo didn't create the release"
for f in "${files[@]}"; do
    curl -s -X POST "${auth[@]}" -H "Content-Type: application/octet-stream" \
        "$api/releases/$id/assets?name=$(basename "$f")" --data-binary @"$f" >/dev/null
    echo "   uploaded $(basename "$f")"
done
echo "== released: https://$host/$repo/releases/tag/$tag"
