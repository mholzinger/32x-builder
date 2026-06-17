#!/usr/bin/env bash
#
# Build + publish a GitHub Release for the current commit.
#
#   scripts/release.sh            # build, confirm, publish
#   scripts/release.sh --dry-run  # show the tag/title/notes, build & publish nothing
#   scripts/release.sh --yes      # skip the interactive confirmation
#   make publish                  # same as scripts/release.sh
#
# The ROM (rom/backrooms.32x) is uploaded as the release asset; the notes are
# the commit log since the previous build-* tag. Tag scheme is git-derived:
# build-<commit-count>, matching the BUILD number shown in the in-game CREDITS
# tab. Requires the `gh` CLI, authenticated (gh auth login).
set -euo pipefail

cd "$(dirname "$0")/.."

DRY_RUN=0
ASSUME_YES=0
for arg in "$@"; do
  case "$arg" in
    --dry-run) DRY_RUN=1 ;;
    --yes|-y)  ASSUME_YES=1 ;;
    *) echo "unknown arg: $arg" >&2; exit 2 ;;
  esac
done

ROM="rom/backrooms.32x"
BUILD=$(git rev-list --count HEAD)
SHA=$(git rev-parse --short HEAD)
DATE=$(date +%Y-%m-%d)
TAG="build-${BUILD}"
TITLE="Build ${BUILD} — ${DATE} (${SHA})"

# Release notes: commits since the previous build-* tag, else full history.
PREV=$(git tag --list 'build-*' --sort=-creatordate | head -1 || true)
if [ -n "$PREV" ]; then RANGE="${PREV}..HEAD"; else RANGE=""; fi
# shellcheck disable=SC2086
NOTES=$(git log --no-merges --pretty='- %s (%h)' ${RANGE})
[ -n "$NOTES" ] || NOTES="- (no new commits since ${PREV})"

echo "  tag:    ${TAG}"
echo "  title:  ${TITLE}"
echo "  asset:  ${ROM}"
echo "  notes${PREV:+ (since $PREV)}:"
echo "${NOTES}" | sed 's/^/    /'
echo

if [ "$DRY_RUN" -eq 1 ]; then
  echo "(dry run — nothing built or published)"
  exit 0
fi

# Real publish: require a clean tree so the release matches a committed state.
if ! git diff --quiet || ! git diff --cached --quiet; then
  echo "error: uncommitted changes — commit them before publishing." >&2
  exit 1
fi
if gh release view "$TAG" >/dev/null 2>&1; then
  echo "error: release ${TAG} already exists — make a new commit first." >&2
  exit 1
fi

echo "==> Building ${ROM} fresh"
make release >/dev/null
[ -f "$ROM" ] || { echo "error: $ROM missing after build" >&2; exit 1; }

if [ "$ASSUME_YES" -ne 1 ]; then
  printf "Publish %s to GitHub? [y/N] " "$TAG"
  read -r ans
  case "$ans" in y|Y) ;; *) echo "aborted."; exit 1 ;; esac
fi

gh release create "$TAG" "$ROM" \
  --title "$TITLE" \
  --notes "$NOTES" \
  --target "$(git rev-parse HEAD)"

echo "==> Published ${TAG}"
