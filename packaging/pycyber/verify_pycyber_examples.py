#!/usr/bin/env python3

import argparse
import os
import subprocess
import sys
import tempfile
import textwrap


def run_snippet(python, title, code, timeout_s):
    result = subprocess.run(
        [python, "-c", code],
        text=True,
        capture_output=True,
        timeout=timeout_s,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            "{} failed with exit code {}\nstdout:\n{}\nstderr:\n{}".format(
                title, result.returncode, result.stdout, result.stderr
            )
        )
    if result.stdout:
        sys.stdout.write(result.stdout)
    if result.stderr:
        sys.stderr.write(result.stderr)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--python", default=None)
    parser.add_argument("--wheel", required=True)
    args = parser.parse_args()

    python = args.python or sys.executable

    with tempfile.TemporaryDirectory(prefix="pycyber-verify-") as temp_dir:
        venv_dir = os.path.join(temp_dir, "venv")
        subprocess.run([python, "-m", "venv", venv_dir], check=True)
        venv_python = os.path.join(venv_dir, "bin", "python")
        venv_pip = os.path.join(venv_dir, "bin", "pip")
        subprocess.run([venv_pip, "install", "--upgrade", "pip"], check=True)
        subprocess.run([venv_pip, "install", args.wheel], check=True)

        run_snippet(
            venv_python,
            "import surface",
            textwrap.dedent(
                """
                import pycyber
                from pycyber import cyber, cyber_time, cyber_timer, parameter, record
                from pycyber.proto import record_pb2

                print(pycyber.__file__)
                print(
                    cyber.ok,
                    cyber_time.Time,
                    cyber_timer.Timer,
                    parameter.Parameter,
                    record.RecordReader,
                    record_pb2.Header.DESCRIPTOR.full_name,
                )
                """
            ),
            30,
        )

        run_snippet(
            venv_python,
            "time surface",
            textwrap.dedent(
                """
                from pycyber import cyber_time

                ct = cyber_time.Time(123)
                td = cyber_time.Duration(111)
                rt = cyber_time.Rate(111)
                assert ct.to_nsec() == 123
                assert td.to_nsec() == 111
                print(ct.to_nsec())
                print(cyber_time.Time.now().to_sec())
                print(cyber_time.Time.mono_time().to_sec())
                print(td.to_nsec(), td.to_sec(), td.iszero())
                print(rt)
                """
            ),
            30,
        )

        run_snippet(
            venv_python,
            "timer surface",
            textwrap.dedent(
                """
                import time

                from pycyber import cyber, cyber_timer

                counter = {"value": 0}

                def cb():
                    counter["value"] += 1

                cyber.init("pycyber_timer_verify")
                with cyber_timer.Timer(10, cb, 0) as timer:
                    timer.start()
                    time.sleep(0.1)
                    timer.stop()
                    assert counter["value"] > 0
                    print(counter["value"])
                cyber.shutdown()
                """
            ),
            30,
        )

        run_snippet(
            venv_python,
            "parameter surface",
            textwrap.dedent(
                """
                from pycyber import cyber, parameter

                cyber.init("pycyber_parameter_verify")

                service_name = "global_parameter_service"
                with cyber.Node(service_name) as node:
                    with parameter.ParameterServer(node) as srv, parameter.ParameterClient(node, service_name) as clt:
                        with parameter.Parameter("author_name", "WanderingEarth") as author_name, parameter.Parameter("author_age", 5000) as author_age, parameter.Parameter("author_score", 888.88) as author_score:
                            clt.set_parameter(author_name)
                            clt.set_parameter(author_age)
                            clt.set_parameter(author_score)

                            clt_params = clt.get_paramslist()
                            srv_params = srv.get_paramslist()
                            assert len(clt_params) == 3
                            assert len(srv_params) == 3
                            print(len(clt_params), len(srv_params))

                cyber.shutdown()
                """
            ),
            30,
        )

        run_snippet(
            venv_python,
            "record surface",
            textwrap.dedent(
                """
                import os

                from google.protobuf.descriptor_pb2 import FileDescriptorProto
                from pycyber import record
                from pycyber.proto import record_pb2

                record_path = {record_path!r}

                writer = record.RecordWriter()
                assert writer.open(record_path)

                msg = record_pb2.Header(
                    major_version=1,
                    minor_version=0,
                    chunk_interval=10,
                    segment_interval=20,
                    is_complete=True,
                )

                file_desc = msg.DESCRIPTOR.file
                proto = FileDescriptorProto()
                file_desc.CopyToProto(proto)
                proto.name = file_desc.name
                desc_str = proto.SerializeToString()

                writer.write_channel(
                    "pycyber_header", msg.DESCRIPTOR.full_name, desc_str
                )
                writer.write_message("pycyber_header", msg, 123, False)
                writer.close()

                reader = record.RecordReader(record_path)
                messages = list(reader.read_messages())
                assert len(messages) == 1
                topic, data, dtype, ts = messages[0]
                assert topic == "pycyber_header"
                assert dtype == msg.DESCRIPTOR.full_name
                assert ts == 123

                parsed = record_pb2.Header()
                parsed.ParseFromString(data)
                assert parsed.major_version == 1
                assert parsed.is_complete
                print(topic, dtype, ts)
                """
            ).format(record_path=os.path.join(temp_dir, "verify.record")),
            30,
        )


if __name__ == "__main__":
    main()
