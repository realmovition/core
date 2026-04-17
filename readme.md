## Core
Core is a fork of Apollo cyber, a publish-subscribe system used as middleware for autonomous driving.

## Benchmark
Latency of publish and subscribe messages.

## How to build

1. Deploy build env.

```bash
sudo bash scripts/deploy/build.sh
```

2. Run the build script.

```bash
bash scripts/build.sh
```

For a narrower `pycyber`-focused build/test loop, use the existing Bazel targets directly:

```bash
bazel test --color=no --curses=no \
  //cyber/python/internal:py_cyber_test \
  //cyber/python/internal:py_record_test \
  //cyber/python/cyber_py3/test:init_test \
  //cyber/python/cyber_py3/test:cyber_test \
  //cyber/python/cyber_py3/test:cyber_time_test \
  //cyber/python/cyber_py3/test:record_test
```

## Example

1. Set Environment Variable
   ```bash
   source scripts/env/runtime.bash
   ```

2. Run the Publisher and Subscriber
    - In the first terminal, run `./bazel-bin/cyber/examples/listener` to start the subscriber program.
    - In the second terminal, run `./bazel-bin/cyber/examples/talker` to start the publisher program.

3. Check the log in `data/log`

## Build the pycyber wheel

Recommended: use the included one-step packaging script which stages, builds, repairs and validates the wheel. Always run packaging inside an isolated Python environment (venv) or a clean container to avoid dependency conflicts.

Quick start (recommended)

1. Create and activate a virtual environment (venv):

```bash
python3 -m venv .packaging-venv
source .packaging-venv/bin/activate
python -m pip install --upgrade pip
pip install build auditwheel twine setuptools_scm
# On Linux, also install patchelf
sudo apt-get update && sudo apt-get install -y patchelf
```

2. Run the one-step packaging script. The script will use PYCYBER_VERSION if set, otherwise it reads the most recent tag matching `pycyber-v*`:

```bash
# Preferred: use tag-derived version (make and push a tag first)
./scripts/release/build_and_package_pycyber.sh

# Or override version explicitly:
PYCYBER_VERSION=0.0.7 ./scripts/release/build_and_package_pycyber.sh
```

Artifacts and checks:

- Artifacts are written to `packaging/pycyber/wheelhouse/`.
- The script runs `twine check` and the repository's `validate_pycyber.py` smoke test.
- A `SHA256SUMS` file is created in the wheelhouse.

Script flags:

- `--skip-auditwheel` : Skip `auditwheel` repair (useful on non-Linux machines).
- `--skip-validate` : Skip running the smoke validation.
- `--skip-sdist` : Only build wheel, skip sdist.
- `--skip-build` : Skip staging (`build_pycyber.py`), useful if staging already exists.
- `--outdir DIR` : Write artifacts to a custom directory.

Manual (step-by-step) packaging

If you prefer to run the steps manually, follow these commands (run inside a clean venv / container):

1) Stage the package from Bazel outputs:

```bash
python3 scripts/release/build_pycyber.py
```

2) Build sdist + wheel:

```bash
python3 -m build --sdist --wheel --outdir packaging/pycyber/dist packaging/pycyber
```

3) Repair Linux wheels (auditwheel):

```bash
auditwheel repair packaging/pycyber/dist/*.whl -w packaging/pycyber/wheelhouse
```

4) Check metadata and run validation:

```bash
python3 -m twine check packaging/pycyber/wheelhouse/*
python3 scripts/release/validate_pycyber.py --wheel packaging/pycyber/wheelhouse/*.whl
```

5) Upload to TestPyPI (recommended first):

```bash
TWINE_USERNAME=__token__ TWINE_PASSWORD=$TEST_PYPI_TOKEN \
  python -m twine upload --repository testpypi packaging/pycyber/wheelhouse/*

# Test install from TestPyPI in a fresh venv:
pip install --index-url https://test.pypi.org/simple/ --no-deps pycyber==0.0.7
```

6) Final upload to PyPI:

```bash
TWINE_USERNAME=__token__ TWINE_PASSWORD=$PYPI_TOKEN \
  python -m twine upload packaging/pycyber/wheelhouse/*
```

Versioning

- `pyproject.toml` is configured to use `setuptools_scm` with the tag pattern `^pycyber-v(?P<version>.+)$`.
- Preferred release flow: create an annotated tag on the release commit and push it:

```bash
git tag -a pycyber-v1.2.3 -m "Release 1.2.3"
git push origin --tags
```

- The one-step script will extract the version from the tag automatically. If needed, override with `PYCYBER_VERSION`.

Troubleshooting

- pip dependency conflicts (for example: `streamlit` vs `protobuf`) occur when build tools are installed into the global Python environment. Always use a clean virtualenv (`venv`) or container for packaging.
- For reproducible manylinux wheels and multi-Python builds, use `cibuildwheel` or the official manylinux Docker images: https://github.com/pypa/manylinux
- If `auditwheel` cannot patch RPATH because `patchelf` is missing, install `patchelf` (Debian/Ubuntu: `apt-get install -y patchelf`) or run the packaging inside an appropriate Linux build environment.

Security and best practices

- Use PyPI API tokens (username `__token__`) with minimal scope; store tokens as secrets in CI.
- Upload to TestPyPI first to verify installation before publishing to the real PyPI.
- Tag releases and attach built artifacts and `SHA256SUMS` to GitHub Releases.
- Optionally GPG-sign artifacts for extra authenticity.

CI

- The repository includes `.github/workflows/release-pycyber.yml` which is triggered by tags `pycyber-v*`. The workflow checks out the full history (`fetch-depth: 0`) so `setuptools_scm` can compute versions correctly.

