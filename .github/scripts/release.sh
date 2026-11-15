#!/usr/bin/env bash
# Called by .github/workflows/release.yml on each push to main.
# Needs GH_TOKEN and GITHUB_REPOSITORY (both set by Actions).
set -euo pipefail

sha=$(git rev-parse HEAD)
if git tag --points-at "$sha" | grep -q '^v'; then
    echo "$sha is already tagged; nothing to do."
    exit 0
fi

last=$(git describe --tags --abbrev=0 --match 'v[0-9]*.[0-9]*.[0-9]*' 2>/dev/null || true)
labels=$(gh api "repos/$GITHUB_REPOSITORY/commits/$sha/pulls" --jq '.[0].labels[].name' 2>/dev/null || true)

if [[ -z "$last" ]]; then
    next=0.1.0
else
    IFS=. read -r major minor patch <<<"${last#v}"
    if grep -qx major <<<"$labels"; then
        next=$((major + 1)).0.0
    elif grep -qx minor <<<"$labels"; then
        next=$major.$((minor + 1)).0
    else
        next=$major.$minor.$((patch + 1))
    fi
fi

echo "Releasing v$next (previous: ${last:-none}) at $sha"
gh release create "v$next" --target "$sha" --title "v$next" --generate-notes \
    ${last:+--notes-start-tag "$last"}
