#!/usr/bin/env bash
set -euo pipefail

# Create an isolated venv, install the built pycyber wheel, run API doc generator
# Usage: scripts/release/install_and_generate_docs.sh [--record-file PATH] [--wheel PATH] [--venv DIR] [--out PATH] [--keep-venv] [--build-if-missing]

REPO_ROOT=$(git rev-parse --show-toplevel)
VENV_DIR="$REPO_ROOT/.pycyber-docs-venv"
OUT_FILE="$REPO_ROOT/packaging/pycyber/API_DOCS.md"
WHEEL_PATH=""
KEEP_VENV=false
BUILD_IF_MISSING=false
RECORD_FILE=""

while [ $# -gt 0 ]; do
  case "$1" in
    --record-file) RECORD_FILE="$2"; shift 2 ;;
    --wheel) WHEEL_PATH="$2"; shift 2 ;;
    --venv) VENV_DIR="$2"; shift 2 ;;
    --out) OUT_FILE="$2"; shift 2 ;;
    --keep-venv) KEEP_VENV=true; shift ;;
    --build-if-missing) BUILD_IF_MISSING=true; shift ;;
    -h|--help) echo "Usage: $0 [--record-file PATH] [--wheel PATH] [--venv DIR] [--out PATH] [--keep-venv] [--build-if-missing]"; exit 0 ;;
    *) echo "Unknown arg: $1"; exit 1 ;;
  esac
done

if [ -n "$RECORD_FILE" ]; then
  echo "Record file: $RECORD_FILE"
else
  echo "Record file: [not provided]"
fi
echo "Output doc: $OUT_FILE"

# Find wheel if not specified
if [ -z "${WHEEL_PATH}" ]; then
  CANDIDATES=("$REPO_ROOT/packaging/pycyber/wheelhouse"/*.whl)
  if [ -e "${CANDIDATES[0]}" ]; then
    WHEEL_PATH="${CANDIDATES[0]}"
  fi
fi

if [ -z "${WHEEL_PATH}" ]; then
  if [ "$BUILD_IF_MISSING" = true ]; then
    echo "No wheel found; running build_and_package_pycyber.sh (this may be slow/require Bazel)..."
    "$REPO_ROOT/scripts/release/build_and_package_pycyber.sh"
    CANDIDATES=("$REPO_ROOT/packaging/pycyber/wheelhouse"/*.whl)
    if [ -e "${CANDIDATES[0]}" ]; then
      WHEEL_PATH="${CANDIDATES[0]}"
    fi
  fi
fi

if [ -z "${WHEEL_PATH}" ]; then
  echo "No wheel found in packaging/pycyber/wheelhouse. Please run scripts/release/build_and_package_pycyber.sh first or pass --wheel PATH" >&2
  exit 2
fi

echo "Using wheel: $WHEEL_PATH"

# Create venv if not exists
if [ ! -d "$VENV_DIR" ]; then
  echo "Creating venv at $VENV_DIR"
  python3 -m venv "$VENV_DIR"
fi

# Activate and install
# shellcheck disable=SC1091
source "$VENV_DIR/bin/activate"
python -m pip install --upgrade pip setuptools >/dev/null

echo "Installing wheel into venv..."
python -m pip install "$WHEEL_PATH"

# Run generator
echo "Running API doc generator inside venv..."
GENERATOR_ARGS=(--out "$OUT_FILE")
if [ -n "$RECORD_FILE" ]; then
  GENERATOR_ARGS+=(--record-file "$RECORD_FILE")
fi
python "$REPO_ROOT/scripts/release/generate_pycyber_api_docs.py" "${GENERATOR_ARGS[@]}"

echo "API docs generated at: $OUT_FILE"

if [ "$KEEP_VENV" = false ]; then
  echo "Deactivating and removing temporary venv: $VENV_DIR"
  deactivate || true
  rm -rf "$VENV_DIR"
else
  echo "Keeping venv at: $VENV_DIR"
  deactivate || true
fi

exit 0
