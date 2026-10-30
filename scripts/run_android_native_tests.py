#!/usr/bin/env python3
"""Run isolated native test executables on the selected adb device."""
import argparse
import pathlib
import shlex
import subprocess
import time
import xml.etree.ElementTree as ET

TESTS = ("fileio_error_test", "setting_persistence_test", "input_test", "audio_test")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binaries", type=pathlib.Path)
    parser.add_argument("--serial", required=True, help="Explicit emulator/device serial")
    parser.add_argument("--report", type=pathlib.Path, default=pathlib.Path("android-native-results.xml"))
    args = parser.parse_args()
    files = [args.binaries / name for name in (*TESTS, "libSDL2.so")]
    for path in files:
        if not path.is_file():
            parser.error(f"missing native binary: {path}")
    adb = ["adb", "-s", args.serial]

    def command(*parts, **kwargs):
        return subprocess.run([*adb, *parts], text=True, capture_output=True, timeout=120, **kwargs)

    root = command("shell", "mktemp -d /data/local/tmp/xm8-native.XXXXXX", check=True).stdout.strip()
    if not root.startswith("/data/local/tmp/xm8-native.") or not root.rsplit("/", 1)[-1].replace(".", "").replace("-", "").isalnum():
        raise RuntimeError("unexpected device test directory")
    suite = ET.Element("testsuite", name="android-native", tests=str(len(TESTS)))
    failures = 0
    try:
        for path in files:
            command("push", str(path), root + "/", check=True)
        command("shell", "chmod 700 " + " ".join(shlex.quote(root + "/" + name) for name in TESTS), check=True)
        for name in TESTS:
            case = ET.SubElement(suite, "testcase", name=name, classname="android.native")
            start = time.monotonic()
            try:
                shell = "cd " + shlex.quote(root) + " && " + shlex.join([
                    "env", "TMPDIR=" + root, "LD_LIBRARY_PATH=" + root, "./" + name])
                result = command("shell", shell)
                output = result.stdout + result.stderr
                print(f"{name}: exit {result.returncode}\n{output}", flush=True)
                ET.SubElement(case, "system-out").text = output
                if result.returncode:
                    failures += 1
                    ET.SubElement(case, "failure", message=f"exit {result.returncode}").text = output
            except subprocess.TimeoutExpired as error:
                failures += 1
                ET.SubElement(case, "failure", message="timeout").text = str(error)
            case.set("time", f"{time.monotonic() - start:.3f}")
    except Exception as error:
        failures += 1
        case = ET.SubElement(suite, "testcase", name="harness", classname="android.native")
        ET.SubElement(case, "failure", message="test setup or execution failed").text = str(error)
        raise
    finally:
        suite.set("tests", str(len(suite.findall("testcase"))))
        suite.set("failures", str(failures))
        args.report.parent.mkdir(parents=True, exist_ok=True)
        ET.ElementTree(suite).write(args.report, encoding="utf-8", xml_declaration=True)
        # Only the unique directory created by this invocation is removed.
        command("shell", "rm -rf " + shlex.quote(root), check=True)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
