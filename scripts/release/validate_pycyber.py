#!/usr/bin/env python3

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Optional


def run(*args: str) -> None:
    subprocess.run(args, check=True)


def resolve_python_executable(python: Optional[str]) -> str:
    return python or sys.executable


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--python",
        default=None,
        help="Python interpreter used to create the validation virtualenv.",
    )
    parser.add_argument("--wheel", required=True)
    args = parser.parse_args()

    wheel = Path(args.wheel).resolve()
    python_executable = resolve_python_executable(args.python)
    with tempfile.TemporaryDirectory(prefix="pycyber-venv-") as temp_dir:
        venv_dir = Path(temp_dir) / "venv"
        run(python_executable, "-m", "venv", str(venv_dir))
        python = venv_dir / "bin" / "python"
        pip = venv_dir / "bin" / "pip"
        run(str(pip), "install", "--upgrade", "pip")
        run(str(pip), "install", str(wheel))
        run(
            str(python),
            "-c",
            (
                "import pycyber; "
                "from pycyber import cyber, cyber_time, cyber_timer, parameter, record; "
                "from pycyber.proto import record_pb2; "
                "print(pycyber.__file__); "
                "print(cyber.ok, cyber_time.Time, cyber_timer.Timer, parameter.Parameter, record.RecordReader, record_pb2.Header.DESCRIPTOR.full_name)"
            ),
        )


if __name__ == "__main__":
    main()
