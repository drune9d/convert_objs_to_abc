#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
REPO="${GITHUB_REPOSITORY:-drune9d/convert_objs_to_abc}"
TAG="${1:-}"
ZIP_PATH="$ROOT_DIR/dist/OBJ-Sequence-to-Alembic-macOS.zip"

if [ -z "$TAG" ]; then
  echo "Usage: scripts/publish_release.sh <tag>"
  echo "Example: scripts/publish_release.sh v1.0.0"
  exit 1
fi

if ! command -v gh >/dev/null 2>&1; then
  echo "GitHub CLI is required to publish releases."
  echo "Install it with: brew install gh"
  exit 1
fi

if ! gh auth status >/dev/null 2>&1; then
  echo "GitHub CLI is not authenticated."
  echo "Run: gh auth login"
  exit 1
fi

"$ROOT_DIR/scripts/package_release.sh"

if git rev-parse "$TAG" >/dev/null 2>&1; then
  echo "Using existing tag: $TAG"
else
  git tag "$TAG"
fi

git push origin "$TAG"

gh release create "$TAG" "$ZIP_PATH" \
  --repo "$REPO" \
  --draft \
  --title "OBJ Sequence to Alembic $TAG" \
  --notes "## What's new in $TAG

### Changing topology
- Each frame is now written with its own full topology. Sequences whose vertex and face counts change over time — fracture, fluid, and remeshing simulations — are preserved correctly instead of being frozen to the first frame's topology (which previously made later pieces disappear).

### From earlier 1.2.x
- Restored animation that was frozen to a single frame, and made Rebuild work when the app is launched from Finder.
- Fixed face winding, and only write UVs when the OBJ actually contains them.
- Bounded prefetch pipeline that overlaps reading and writing across CPU cores.
- Determinate progress bar with a per-frame counter, plus clearer log diagnostics."

echo
echo "Draft release created for $TAG"
