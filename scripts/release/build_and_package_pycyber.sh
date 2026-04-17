#!/usr/bin/env bash
set -euo pipefail

# Build and package pycyber, run auditwheel repair and smoke validation.
# After success, artifacts are placed in packaging/pycyber/wheelhouse.
# Usage: ./scripts/release/build_and_package_pycyber.sh [--skip-auditwheel] [--skip-validate] [--skip-sdist] [--skip-build] [--outdir DIR]

SKIP_AUDITWHEEL=false
SKIP_VALIDATE=false
SKIP_SDIST=false
SKIP_BUILD=false
OUTDIR="packaging/pycyber/wheelhouse"

while [ $# -gt 0 ]; do
  case "$1" in
    --skip-auditwheel) SKIP_AUDITWHEEL=true; shift ;;
    --skip-validate) SKIP_VALIDATE=true; shift ;;
    --skip-sdist) SKIP_SDIST=true; shift ;;
    --skip-build) SKIP_BUILD=true; shift ;;
    --outdir) OUTDIR="$2"; shift 2 ;;
    -h|--help) echo "Usage: $0 [--skip-auditwheel] [--skip-validate] [--skip-sdist] [--skip-build] [--outdir DIR]"; exit 0 ;;
    *) echo "Unknown arg: $1"; exit 1 ;;
  esac
done

echo "Starting pycyber build+package at $(date -u +%Y-%m-%dT%H:%M:%SZ)"

REPO_ROOT=$(git rev-parse --show-toplevel)
cd "$REPO_ROOT"

echo "Fetching tags from origin..."
git fetch --tags --prune origin || true

# Determine version from env or tag
if [ -n "${PYCYBER_VERSION:-}" ]; then
  VERSION="$PYCYBER_VERSION"
else
  TAG=$(git describe --tags --match 'pycyber-v*' --abbrev=0 2>/dev/null || true)
  if [ -z "$TAG" ]; then
    echo "Error: no tag matching 'pycyber-v*' found and PYCYBER_VERSION not set." >&2
    echo "Create a tag like: git tag -a pycyber-v1.2.3 -m 'Release 1.2.3' && git push origin --tags" >&2
    exit 1
  fi
  VERSION=${TAG#pycyber-v}
fi
echo "Detected version: $VERSION (tag: ${TAG:-N/A})"

# Ensure Python build tools are available
echo "Installing/ensuring Python build tools (build, auditwheel, twine, setuptools_scm)..."
python3 -m pip install --upgrade pip >/dev/null || true
python3 -m pip install --upgrade build auditwheel twine setuptools_scm >/dev/null || true

# Check patchelf on Linux for auditwheel
if [ "$(uname -s)" = "Linux" ]; then
  if ! command -v patchelf >/dev/null 2>&1; then
    echo "Warning: patchelf not found. Install it (e.g. apt-get install -y patchelf) for auditwheel to patch RPATHs." >&2
  fi
fi

STAGING_DIR=packaging/pycyber/staging
DIST_DIR=packaging/pycyber/dist
WHEELHOUSE_DIR="$OUTDIR"

# Clean previous outputs
rm -rf "$STAGING_DIR" "$DIST_DIR" "$WHEELHOUSE_DIR"
mkdir -p "$DIST_DIR" "$WHEELHOUSE_DIR"

# Stage package (generates staging/src/pycyber)
if [ "$SKIP_BUILD" = false ]; then
  echo "Staging pycyber package (this may run bazel build)..."
  python3 scripts/release/build_pycyber.py || { echo "Staging failed" >&2; exit 1; }
else
  echo "Skipping staging (--skip-build)"
fi

# Build wheel (and sdist unless skipped)
BUILD_ARGS=(--wheel)
if [ "$SKIP_SDIST" = false ]; then
  BUILD_ARGS=(--sdist --wheel)
fi

echo "Running: python3 -m build ${BUILD_ARGS[*]} --outdir $DIST_DIR packaging/pycyber"
python3 -m build "${BUILD_ARGS[@]}" --outdir "$DIST_DIR" packaging/pycyber || { echo "Build failed" >&2; exit 1; }

# Repair wheels with auditwheel on Linux
if [ "$SKIP_AUDITWHEEL" = false ] && [ "$(uname -s)" = "Linux" ] && command -v auditwheel >/dev/null 2>&1; then
  echo "Running auditwheel.repair for Linux wheels..."
  repaired=false
  for whl in "$DIST_DIR"/*.whl; do
    [ -e "$whl" ] || continue
    echo "Repairing $whl"
    auditwheel repair "$whl" -w "$WHEELHOUSE_DIR" || { echo "auditwheel repair failed for $whl" >&2; exit 1; }
    repaired=true
  done
  if [ "$repaired" = false ]; then
    echo "No wheels were repaired; copying built wheels to wheelhouse"
    cp "$DIST_DIR"/*.whl "$WHEELHOUSE_DIR"/ || true
  fi
else
  echo "Skipping auditwheel (not requested or not available). Copying built wheels to wheelhouse"
  cp "$DIST_DIR"/*.whl "$WHEELHOUSE_DIR"/ || true
fi

# Copy sdist to wheelhouse if present
shopt -s nullglob || true
for s in "$DIST_DIR"/*.tar.gz; do
  cp "$s" "$WHEELHOUSE_DIR"/ || true
done

# Run twine check
echo "Running twine check on artifacts..."
python3 -m twine check "$WHEELHOUSE_DIR"/* || { echo "twine check failed" >&2; exit 1; }

# Run smoke validation using the provided validation script
if [ "$SKIP_VALIDATE" = false ]; then
  WHEELS=( "$WHEELHOUSE_DIR"/*.whl )
  if [ ${#WHEELS[@]} -eq 0 ]; then
    echo "No wheel found in $WHEELHOUSE_DIR to validate" >&2
    exit 1
  fi
  VALIDATE_WHEEL="${WHEELS[0]}"
  echo "Validating wheel: $VALIDATE_WHEEL"
  python3 scripts/release/validate_pycyber.py --wheel "$VALIDATE_WHEEL" || { echo "Validation failed" >&2; exit 1; }
else
  echo "Skipping validation (--skip-validate)"
fi

# Create SHA256SUMS
if command -v sha256sum >/dev/null 2>&1; then
  echo "Creating SHA256SUMS in $WHEELHOUSE_DIR"
  (cd "$WHEELHOUSE_DIR" && sha256sum * > SHA256SUMS) || true
else
  echo "sha256sum not found - skipping SHA256SUMS generation"
fi

echo "Build and validation finished successfully. Artifacts are in: $WHEELHOUSE_DIR"
echo "To upload to PyPI: python3 -m twine upload $WHEELHOUSE_DIR/*"
echo "To upload to TestPyPI: python3 -m twine upload --repository testpypi $WHEELHOUSE_DIR/*"

exit 0
