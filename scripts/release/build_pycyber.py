#!/usr/bin/env python3

import shutil
import subprocess
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
STAGING_ROOT = REPO_ROOT / "packaging" / "pycyber" / "staging"
SRC_ROOT = STAGING_ROOT / "src" / "pycyber"
INTERNAL_ROOT = SRC_ROOT / "internal"
CONF_ROOT = SRC_ROOT / "conf"
PROTO_ROOT = SRC_ROOT / "proto"

PY_MODULES = [
    "__init__.py",
    "cyber.py",
    "cyber_time.py",
    "cyber_timer.py",
    "parameter.py",
    "record.py",
]

EXTENSION_TARGETS = {
    "//cyber/python/internal:_cyber_wrapper.so": "_cyber_wrapper.so",
    "//cyber/python/internal:_cyber_record_wrapper.so": "_cyber_record_wrapper.so",
    "//cyber/python/internal:_cyber_time_wrapper.so": "_cyber_time_wrapper.so",
    "//cyber/python/internal:_cyber_timer_wrapper.so": "_cyber_timer_wrapper.so",
    "//cyber/python/internal:_cyber_parameter_wrapper.so": "_cyber_parameter_wrapper.so",
}


def run(*args: str) -> None:
    subprocess.run(args, cwd=REPO_ROOT, check=True)


def resolve_single_bazel_output(target: str) -> Path:
    output = subprocess.check_output(
        [
            "bazel",
            "cquery",
            target,
            "--output=starlark",
            '--starlark:expr="\\n".join([f.path for f in target.files.to_list()])',
        ],
        cwd=REPO_ROOT,
        text=True,
    ).strip().splitlines()
    if len(output) != 1:
        raise RuntimeError(f"expected one output for {target}, got {output}")
    return REPO_ROOT / output[0]


def stage_python_modules() -> None:
    source_root = REPO_ROOT / "cyber" / "python" / "cyber_py3"
    for module_name in PY_MODULES:
        content = (source_root / module_name).read_text()
        content = content.replace(
            "from cyber.python.cyber_py3 import ", "from pycyber import "
        )
        content = content.replace("../internal", "internal")
        (SRC_ROOT / module_name).write_text(content)


def stage_extensions() -> None:
    run("bazel", "build", *EXTENSION_TARGETS.keys())
    bazel_bin = REPO_ROOT / "bazel-bin" / "cyber" / "python" / "internal"
    for output_name in EXTENSION_TARGETS.values():
        shutil.copy2(bazel_bin / output_name, INTERNAL_ROOT / output_name)


def stage_conf() -> None:
    (CONF_ROOT / "__init__.py").write_text("")
    for conf_file in (REPO_ROOT / "cyber" / "conf").glob("*.conf"):
        shutil.copy2(conf_file, CONF_ROOT / conf_file.name)


def stage_proto() -> None:
    run("bazel", "build", "//cyber/proto:record_py_pb2")
    (PROTO_ROOT / "__init__.py").write_text('from . import record_pb2\n\n__all__ = ["record_pb2"]\n')
    shutil.copy2(REPO_ROOT / "cyber" / "proto" / "record.proto", PROTO_ROOT / "record.proto")
    shutil.copy2(
        resolve_single_bazel_output("//cyber/proto:record_py_pb2"),
        PROTO_ROOT / "record_pb2.py",
    )


def main() -> None:
    if STAGING_ROOT.exists():
        shutil.rmtree(STAGING_ROOT)
    INTERNAL_ROOT.mkdir(parents=True, exist_ok=True)
    CONF_ROOT.mkdir(parents=True, exist_ok=True)
    PROTO_ROOT.mkdir(parents=True, exist_ok=True)
    (INTERNAL_ROOT / "__init__.py").write_text("")

    stage_python_modules()
    stage_extensions()
    stage_conf()
    stage_proto()


if __name__ == "__main__":
    main()
