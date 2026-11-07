"""Windows real-executable UTF-8 argv regression; no converter-only oracle."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import uuid


def run(executable, *arguments, timeout=15):
    result = subprocess.run(
        [str(executable), *arguments], stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, timeout=timeout,
    )
    return result, result.stdout.decode("utf-8"), result.stderr.decode("utf-8")


def check_invalid_utf16(executable):
    for invalid in ("\ud800", "\udc00"):
        result, out, err = run(executable, invalid)
        if result.returncode != 2 or "Invalid UTF-16 command line" not in err:
            raise AssertionError(f"malformed UTF-16 was not rejected: {result.returncode} {out!r} {err!r}")


def stop(process):
    if process.poll() is None:
        process.terminate()
    try:
        out, err = process.communicate(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        out, err = process.communicate(timeout=3)
    if process.returncode != 0:
        print(f"child exit={process.returncode} stdout={out!r} stderr={err!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--probe", required=True, type=Path)
    args = parser.parse_args()
    if os.name != "nt":
        raise RuntimeError("this regression must launch the Windows executable")
    check_invalid_utf16(args.exe)
    name = "\u65e5\u672c \U0001f680 \"252\"-" + uuid.uuid4().hex[:8]
    with tempfile.TemporaryDirectory(prefix="nozzle-argv-") as directory:
        unicode_dir = Path(directory) / "\u65e5\u672c-\U0001f680"
        unicode_dir.mkdir()
        expected = unicode_dir / "\u540d\U0001f680.txt"
        evidence = unicode_dir / "\u8a3c\U0001f680.json"
        expected.write_text(name, encoding="utf-8")
        source = "missing-" + name
        mixer = subprocess.Popen([
            str(args.exe), "--smoke-forward", "--source", source, "--output", name,
            "--width", "320", "--height", "240", "--min-frames", "1",
            "--publish-frames", "1", "--timeout-ms", "5000",
            "--hold-ms", "0", "--evidence", str(evidence),
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            found, out, err = run(args.probe, str(expected), timeout=10)
            if found.returncode != 0 or out.splitlines() != [name]:
                raise AssertionError(f"output discovery identity mismatch: {out!r} {err!r}")
            out, err = mixer.communicate(timeout=15)
            if mixer.returncode != 1:
                raise AssertionError(f"missing input should fail: {out!r} {err!r}")
            record = json.loads(evidence.read_text(encoding="utf-8"))
            if (record["failure_reason"] != "receiver_create_timeout"
                    or record["input"]["source_name"] != source
                    or record["output"]["name"] != name
                    or record["output"]["published_count"] != 0):
                raise AssertionError(f"source/output identity or failure mismatch: {record!r}")
        finally:
            stop(mixer)
    print("PASS Windows Unicode output registration, input selection and evidence/probe paths; no pixel claim")


if __name__ == "__main__":
    main()
