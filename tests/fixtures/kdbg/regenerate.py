#!/usr/bin/env python3
"""Regenerate the committed KDBG fixtures.

Requires a built KRCompiler `krc` and `krld` (0.1.0 or newer):

    python tests/fixtures/kdbg/regenerate.py \
        --krc C:/path/to/krc.exe --krld C:/path/to/krld.exe

The output is deterministic for the same compiler and source: the source-root
keeps the recorded path relative, and the fixture contains no timestamps.
"""

import argparse
import pathlib
import shutil
import subprocess

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
WORK = ROOT / "build" / "fixtures-gen"


def run(command: list[str]) -> None:
    print(" ".join(command))
    subprocess.run(command, cwd=ROOT, check=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--krc", required=True)
    parser.add_argument("--krld", required=True)
    args = parser.parse_args()

    WORK.mkdir(parents=True, exist_ok=True)
    source = "tests/fixtures/kdbg/fake.c"
    object_path = WORK / "fake.kro"
    image_path = WORK / "fake.kr32"
    json_path = WORK / "fake.krc.json"

    run([args.krc, "--target=kr32", "-c", "-o", str(object_path), source])
    run([args.krld, str(object_path), "-o", str(image_path), "--entry=_start",
         f"--source-root={ROOT}"])
    with json_path.open("w", encoding="utf-8", newline="\n") as output:
        subprocess.run([args.krc, "--dump-symbols", str(image_path), "--json"],
                       cwd=ROOT, check=True, stdout=output)
    text = json_path.read_text(encoding="utf-8-sig").replace("\r\n", "\n").rstrip() + "\n"
    json_path.write_text(text, encoding="utf-8", newline="\n")

    image = image_path.read_bytes()
    magic = image.find(b"KDBG")
    if magic < 0:
        raise SystemExit("no KDBG payload in the linked image")
    (HERE / "fake.kdbg").write_bytes(image[magic:])

    shutil.copyfile(image_path, HERE / "fake.kr32")
    shutil.copyfile(json_path, HERE / "fake.krc.json")
    print(f"wrote {HERE / 'fake.kr32'}, {HERE / 'fake.kdbg'}, {HERE / 'fake.krc.json'}")


if __name__ == "__main__":
    main()
