#!/usr/bin/env python3
"""Generate API documentation for pycyber by introspection and include a record-file parse example.

Produces a Markdown file with:
- pycyber package path and version
- auto-generated reference for selected modules (docstrings, functions, classes)
- a short example that parses a provided .record file and the actual sample outputs

Intended to be run in an environment where pycyber is installed (e.g., a packaging venv).
"""

from __future__ import annotations

import argparse
import importlib
import inspect
import json
import os
import sys
import textwrap
from typing import List

REQUIRED_MODULES = ["pycyber", "pycyber.cyber", "pycyber.record"]
OPTIONAL_MODULES = ["pycyber.proto", "pycyber.proto.record_pb2"]


def modules_to_doc() -> List[str]:
    modules = list(REQUIRED_MODULES)
    for module_name in OPTIONAL_MODULES:
        if not isinstance(safe_import(module_name), Exception):
            modules.append(module_name)
    return modules


def safe_import(module_name: str):
    try:
        return importlib.import_module(module_name)
    except Exception as e:
        return e


def doc_module(module_name: str) -> str:
    mod = safe_import(module_name)
    out_lines: List[str] = []
    out_lines.append(f"## {module_name}\n")

    if isinstance(mod, Exception):
        out_lines.append(f"**Failed to import {module_name}:** {mod}\n")
        return "\n".join(out_lines)

    mod_doc = inspect.getdoc(mod) or ""
    if mod_doc:
        out_lines.append(mod_doc + "\n")

    # Public functions
    funcs = [m for m in inspect.getmembers(mod, inspect.isfunction) if not m[0].startswith("_")]
    if funcs:
        out_lines.append("### Functions\n")
        for name, fn in funcs:
            try:
                sig = str(inspect.signature(fn))
            except Exception:
                sig = "(...)"
            doc = inspect.getdoc(fn) or ""
            out_lines.append(f"#### {name}{sig}\n")
            if doc:
                out_lines.append(textwrap.indent(doc, "    ") + "\n")

    # Public classes
    classes = [m for m in inspect.getmembers(mod, inspect.isclass) if not m[0].startswith("_")]
    if classes:
        out_lines.append("### Classes\n")
        for name, cls in classes:
            cls_doc = inspect.getdoc(cls) or ""
            out_lines.append(f"#### class {name}\n")
            if cls_doc:
                out_lines.append(textwrap.indent(cls_doc, "    ") + "\n")
            # methods
            methods = [m for m in inspect.getmembers(cls, inspect.isfunction) if not m[0].startswith("_")]
            if methods:
                out_lines.append("##### Methods\n")
                for mname, mfn in methods:
                    try:
                        msig = str(inspect.signature(mfn))
                    except Exception:
                        msig = "(...)"
                    mdoc = inspect.getdoc(mfn) or ""
                    out_lines.append(f"- `{mname}{msig}`\n")
                    if mdoc:
                        out_lines.append(textwrap.indent(mdoc, "    ") + "\n")

    return "\n".join(out_lines)


def parse_record_and_sample(record_path: str, channel_limit: int = 20, sample_messages: int = 5) -> str:
    """Use pycyber.record APIs to inspect the record header, channels, and sample messages.

    Returns a Markdown fragment describing the parsed outputs and a usage snippet.
    """
    parts: List[str] = []

    try:
        import pycyber
        from pycyber import record
    except Exception as exc:
        parts.append(f"**Error importing pycyber (required to parse record):** {exc}\n")
        return "\n".join(parts)

    record_pb2 = None
    try:
        from pycyber.proto import record_pb2 as loaded_record_pb2

        record_pb2 = loaded_record_pb2
    except Exception:
        pass

    parts.append("### Record parsing example and output\n")

    if record_pb2 is not None:
        example_snippet = textwrap.dedent(
            """
            from pycyber import record
            from pycyber.proto import record_pb2

            reader = record.RecordReader('RECORD_PATH')
            header = record_pb2.Header()
            header.ParseFromString(reader.get_headerstring())
            channels = list(reader.get_channellist())

            print('header:', header)
            print('channels:', channels[:20])

            for i, message in enumerate(reader.read_messages()):
                print(f'{i+1}. channel={message.topic} type={message.data_type} timestamp={message.timestamp} payload_bytes={len(message.message)}')
                if i + 1 >= 5:
                    break
            """
        ).strip()
    else:
        example_snippet = textwrap.dedent(
            """
            from pycyber import record

            reader = record.RecordReader('RECORD_PATH')
            header_bytes = reader.get_headerstring()
            channels = list(reader.get_channellist())

            print('header_bytes:', len(header_bytes))
            print('channels:', channels[:20])

            for i, message in enumerate(reader.read_messages()):
                print(f'{i+1}. channel={message.topic} type={message.data_type} timestamp={message.timestamp} payload_bytes={len(message.message)}')
                if i + 1 >= 5:
                    break
            """
        ).strip()

    parts.append("```python\n" + example_snippet.replace("RECORD_PATH", record_path) + "\n```\n")

    if not record_path:
        parts.append("No `--record-file` provided; skipping record parsing sample output.\n")
        return "\n".join(parts)

    try:
        reader = record.RecordReader(record_path)
    except Exception as exc:
        parts.append(f"Failed to open record file {record_path}: {exc}\n")
        return "\n".join(parts)

    try:
        parts.append("#### Header\n")
        parts.append("```")
        if record_pb2 is None:
            parts.append(json.dumps({"header_bytes": len(reader.get_headerstring())}, indent=2))
        else:
            header = record_pb2.Header()
            header.ParseFromString(reader.get_headerstring())
            header_summary = {
                "major": header.major_version,
                "minor": header.minor_version,
                "messages": header.message_number,
                "channels": header.channel_number,
                "chunks": header.chunk_number,
                "begin": header.begin_time,
                "end": header.end_time,
                "complete": header.is_complete,
            }
            parts.append(json.dumps(header_summary, indent=2))
        parts.append("```")
    except Exception as exc:
        parts.append(f"Failed to parse header: {exc}\n")

    try:
        channels = list(reader.get_channellist())
        parts.append(f"#### Channels (showing up to {channel_limit})\n")
        if not channels:
            parts.append("(no channels found)\n")
        else:
            parts.append("```")
            for c in channels[:channel_limit]:
                try:
                    mtype = reader.get_messagetype(c)
                    mcount = reader.get_messagenumber(c)
                except Exception:
                    mtype = "?"
                    mcount = "?"
                parts.append(f"{c} | type={mtype} | count={mcount}")
            parts.append("```")
    except Exception as exc:
        parts.append(f"Failed to enumerate channels: {exc}\n")

    if sample_messages > 0:
        parts.append(f"#### Sample messages (up to {sample_messages})\n")
        parts.append("```")
        sampled = 0
        try:
            for sampled, message in enumerate(reader.read_messages(), start=1):
                parts.append(
                    f"{sampled}. channel={message.topic} type={message.data_type} timestamp={message.timestamp} payload_bytes={len(message.message)}"
                )
                if sampled >= sample_messages:
                    break
            if sampled == 0:
                parts.append("(no messages found)")
        except Exception as exc:
            parts.append(f"Failed to read messages: {exc}")
        parts.append("```")

    return "\n".join(parts)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate pycyber API docs and optionally parse a record file to include real outputs.")
    parser.add_argument("--record-file", default="", help="Optional path to a .record file to parse for examples")
    parser.add_argument("--out", default="packaging/pycyber/API_DOCS.md", help="Output Markdown file")
    parser.add_argument("--channel-limit", type=int, default=20)
    parser.add_argument("--sample-messages", type=int, default=5)

    args = parser.parse_args()

    lines: List[str] = []
    lines.append("# pycyber API reference (auto-generated)\n")

    # package info
    try:
        import pycyber
        pkg_path = getattr(pycyber, "__file__", "(unknown)")
        pkg_version = getattr(pycyber, "__version__", None)
        if pkg_version is None:
            try:
                # try importlib.metadata
                try:
                    from importlib.metadata import version as get_version
                except Exception:
                    from importlib_metadata import version as get_version  # type: ignore
                pkg_version = get_version("pycyber")
            except Exception:
                pkg_version = "(unknown)"
    except Exception as exc:
        pkg_path = None
        pkg_version = None
        lines.append(f"**pycyber import failed:** {exc}\n")

    lines.append(f"**Installed pycyber path**: {pkg_path}\n")
    lines.append(f"**Detected version**: {pkg_version}\n")

    # Document modules
    for mod in modules_to_doc():
        try:
            lines.append(doc_module(mod))
        except Exception as exc:
            lines.append(f"Failed to document module {mod}: {exc}\n")

    # Parse record file and include outputs
    try:
        record_fragment = parse_record_and_sample(args.record_file, channel_limit=args.channel_limit, sample_messages=args.sample_messages)
        lines.append(record_fragment)
    except Exception as exc:
        lines.append(f"Record parse step failed: {exc}\n")

    out_dir = os.path.dirname(args.out)
    if out_dir and not os.path.exists(out_dir):
        os.makedirs(out_dir, exist_ok=True)

    with open(args.out, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))

    print(f"Wrote API docs to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
