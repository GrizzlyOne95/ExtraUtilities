#!/usr/bin/env python3
"""Attach an observational HUD probe to a harness-managed GOG Redux PID.

This does not implement a HUD API, launch the game, or alter draw arguments.
Frida instrumentation is temporary and detached when capture ends.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import threading
import time

ASSETS = Path(__file__).with_name("native_hud_trace")


def pe_fingerprints(data, profile):
    """Map target RVAs to raw bytes only inside executable, file-backed sections."""
    def unpack(fmt, offset):
        try:
            return struct.unpack_from(fmt, data, offset)
        except struct.error as error:
            raise ValueError("Truncated PE header or section table") from error

    if data[:2] != b"MZ":
        raise ValueError("Not a PE executable")
    pe, = unpack("<I", 0x3C)
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Invalid PE signature")
    machine, count = unpack("<HH", pe + 4)
    optional_size, = unpack("<H", pe + 20)
    optional = pe + 24
    magic, = unpack("<H", optional)
    image_base, = unpack("<I", optional + 28)
    if machine != 0x14C or magic != 0x10B or image_base != profile["image_base"]:
        raise ValueError("Expected the qualified x86 PE32 image")
    section_table = optional + optional_size
    sections = []
    for index in range(count):
        row = section_table + index * 40
        _, _, rva, raw_size, raw_offset = unpack("<8sIIII", row)
        characteristics, = unpack("<I", row + 36)
        sections.append((rva, raw_size, raw_offset, characteristics))
    result = {}
    for name, rva in profile["targets"].items():
        for start, size, offset, characteristics in sections:
            if start <= rva and rva + 32 <= start + size and characteristics & 0x20000000:
                raw = offset + rva - start
                fingerprint = data[raw:raw + 32]
                if len(fingerprint) != 32 or not any(fingerprint):
                    raise ValueError("Unbacked target: " + name)
                result[name] = {"rva": rva, "bytes": list(fingerprint)}
                break
        else:
            raise ValueError("Target outside executable file-backed sections: " + name)
    return result


def build_config(executable, every, max_records):
    profile = json.loads((ASSETS / "gog_2_2_301.json").read_text(encoding="utf-8"))
    data = Path(executable).read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != profile["sha256"]:
        raise ValueError("Executable hash is not the static-corpus GOG baseline; refusing attach")
    config = dict(profile)
    config["targets"] = pe_fingerprints(data, profile)
    config.update(every=every, max_records=max_records)
    return config


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True, help="Existing PID from BZRHarness")
    parser.add_argument("--exe", type=Path, required=True, help="Exact executable of that PID")
    parser.add_argument("--output", type=Path, required=True, help="New JSONL capture; never overwrites")
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--every", type=int, default=30, help="Sample every N status render calls")
    parser.add_argument("--max-records", type=int, default=2000)
    args = parser.parse_args()
    if args.pid <= 0 or not 0 < args.seconds <= 600 or args.every < 1 or not 1 <= args.max_records <= 100000:
        parser.error("Require positive PID/every, seconds in (0,600], and max-records in [1,100000]")
    if os.name != "nt":
        parser.error("Capture requires the native Windows Redux process; host tests can run on Linux")
    try:
        config = build_config(args.exe, args.every, args.max_records)
        import frida  # Optional qualification dependency, never an EXU DLL dependency.
    except (OSError, ValueError, ImportError) as error:
        parser.exit(1, str(error) + "\n")

    source = (ASSETS / "capture.js").read_text(encoding="utf-8").replace("__CONFIG__", json.dumps(config))
    stopped = threading.Event()
    ready = threading.Event()
    lock = threading.Lock()
    failures = []
    session = None
    script = None
    try:
        with args.output.open("x", encoding="utf-8") as output:
            def write(value):
                with lock:
                    output.write(json.dumps(value, allow_nan=False) + "\n")
                    output.flush()

            def on_message(message, _data):
                try:
                    value = message.get("payload") if message.get("type") == "send" else message
                    write(value)
                    if message.get("type") == "error":
                        failures.append(message.get("description", "Frida script error"))
                        stopped.set()
                    elif isinstance(value, dict) and value.get("event") == "ready":
                        ready.set()
                    elif isinstance(value, dict) and value.get("event") == "read_error":
                        failures.append(value.get("message", "Native argument read failed"))
                        stopped.set()
                    elif isinstance(value, dict) and value.get("event") == "record_limit":
                        stopped.set()
                except (ValueError, OSError) as error:
                    failures.append(str(error))
                    stopped.set()

            write({"event": "capture_start", "pid": args.pid, "exe": str(args.exe.resolve()),
                   "sha256": config["sha256"], "utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())})
            try:
                session = frida.get_local_device().attach(args.pid)
                session.on("detached", lambda *_: stopped.set())
                script = session.create_script(source)
                script.on("message", on_message)
                script.load()
                if not ready.wait(5) or stopped.is_set():
                    raise RuntimeError("Probe did not pass its live-entry byte checks")
                print("Capturing native HUD arguments to", args.output)
                stopped.wait(args.seconds)
            except KeyboardInterrupt:
                pass
            finally:
                if script is not None:
                    try:
                        script.unload()
                    except frida.InvalidOperationError:
                        pass
                    except Exception as error:
                        failures.append("Script unload: " + str(error))
                if session is not None:
                    try:
                        session.detach()
                    except frida.InvalidOperationError:
                        pass
                    except Exception as error:
                        failures.append("Session detach: " + str(error))
                write({"event": "capture_end", "ready": ready.is_set(), "errors": failures})
        if failures:
            raise RuntimeError("; ".join(failures))
    except Exception as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
