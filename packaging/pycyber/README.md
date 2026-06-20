# pycyber

`pycyber` packages Apollo Cyber RT Python bindings from this repository.

The release flow stages Bazel-built native extensions and Python modules, builds
wheel/sdist artifacts, repairs Linux wheels with `auditwheel`, validates install
and imports in an isolated virtual environment, and runs
`cyber/python/cyber_py3/examples` smoke tests.

For a packaging-side example verification that mirrors the repo examples, run
`python packaging/pycyber/verify_pycyber_examples.py`.

Release CI verifies wheel install/import compatibility on CPython 3.11 across
supported Linux architectures before publish.

Use `scripts/release/build_and_package_pycyber.sh` to produce artifacts under
`packaging/pycyber/wheelhouse/`.
