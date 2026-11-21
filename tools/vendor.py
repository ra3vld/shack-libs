#!/usr/bin/env python3
"""Export or check a commit-pinned source dependency in a consumer repository."""
import argparse
import io
from pathlib import Path
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
COMPONENTS = ("ldg_core", "rotator_core")


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args])


def snapshot(revision, components):
    commit = git("rev-parse", "--verify", f"{revision}^{{commit}}").decode().strip()
    archive = git("archive", commit, *components)
    with tarfile.open(fileobj=io.BytesIO(archive)) as stream:
        files = {Path(entry.name): stream.extractfile(entry).read()
                 for entry in stream if entry.isfile()}
    files[Path("REVISION")] = (commit + "\n").encode()
    return files


def installed(destination):
    return {path.relative_to(destination): path.read_bytes()
            for path in destination.rglob("*") if path.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("consumer", type=Path)
    parser.add_argument("components", nargs="+", choices=COMPONENTS)
    parser.add_argument("--revision", default="HEAD")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    destination = args.consumer / "vendor" / "shack-libs"
    expected = snapshot(args.revision, args.components)
    current = installed(destination)
    if current == expected:
        return
    if args.check:
        changed = sorted(k for k in current.keys() | expected.keys()
                         if current.get(k) != expected.get(k))
        raise SystemExit("Snapshot differs:\n" + "\n".join(map(str, changed)))
    if current:
        try:
            previous = current[Path("REVISION")].decode().strip()
            selected = [name for name in COMPONENTS if (destination / name).is_dir()]
            clean = current == snapshot(previous, selected)
        except (KeyError, subprocess.CalledProcessError):
            clean = False
        if not clean:
            raise SystemExit(f"{destination} contains files outside its pinned snapshot")
    for path in current.keys() - expected.keys():
        (destination / path).unlink()
    for path, content in expected.items():
        output = destination / path
        output.parent.mkdir(parents=True, exist_ok=True)
        if current.get(path) != content:
            output.write_bytes(content)


if __name__ == "__main__":
    main()
