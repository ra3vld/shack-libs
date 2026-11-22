#!/usr/bin/env python3
"""Generate the Python codec and bounded nanopb options from the contract."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = "shack_protocol"


def nanopb_options():
    bindings = json.loads((ROOT / "bindings.json").read_text())
    limits = bindings["limits"]
    ids = (
        "Request.request_id", "Request.node_id", "UsbRequest.request_id",
        "ListDevicesRequest.after_device_id", "GetTunerStateRequest.device_id",
        "Device.device_id", "Catalog.next_after_device_id", "TunerState.device_id",
        "Response.request_id", "Response.node_id",
    )
    methods = [entry.split()[0] for name, entry in bindings["http"].items()
               if not name.endswith("_header")]
    options = {field: f"max_length:{limits['id_bytes']}" for field in ids}
    options["UsbRequest.target"] = f"max_length:{limits['http_target_bytes']}"
    options["UsbRequest.method"] = f"max_length:{max(map(len, methods))}"
    options["Catalog.devices"] = f"max_count:{limits['catalog_page']}"
    return "".join(f"shack.v1.{field} {options[field]}\n" for field in sorted(options)).encode()


def update(path, content, verify):
    if verify:
        if not path.is_file() or path.read_bytes() != content:
            raise SystemExit(f"{path.relative_to(ROOT)} is out of date")
    else:
        path.write_bytes(content)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    schema = ROOT / "schema"
    with tempfile.TemporaryDirectory() as directory:
        subprocess.run(
            [sys.executable, "-m", "grpc_tools.protoc", f"-I{schema}",
             f"--python_out={directory}", str(schema / PACKAGE / "wire.proto")],
            check=True,
        )
        generated = (Path(directory) / PACKAGE / "wire_pb2.py").read_bytes()
    update(ROOT / PACKAGE / "wire_pb2.py", generated, args.check)
    update(schema / PACKAGE / "wire.options", nanopb_options(), args.check)


if __name__ == "__main__":
    main()
