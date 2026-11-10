"""Export the Python Continuous Planner bundles to the native ABCMODL1 format.

The C++ runtime reads only this versioned binary and its manifest; it never
deserializes pickle, PyTorch, or NumPy archives. Source model directories are
left untouched. By default this exports the checked-in Native/models bundles
to lib/models/{32ms,16ms,8ms}.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

import numpy as np

MAGIC = b"ABCMODL1"
VERSION = 1
VARIANTS = {32: "continuous", 16: "continuous-16", 8: "continuous-8"}


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def write_bundle(source: Path, destination: Path, commit: int) -> dict:
    arrays = np.load(source / "weights.npz", allow_pickle=False)
    names = sorted(arrays.files)
    if not names:
        raise ValueError(f"{source} has no tensors")
    destination.mkdir(parents=True, exist_ok=True)
    output = destination / "model.abcmodel"
    with output.open("wb") as f:
        f.write(struct.pack("<8sIIII", MAGIC, VERSION, commit, commit, len(names)))
        for name in names:
            value = np.ascontiguousarray(arrays[name])
            if value.dtype == np.dtype("<f4") or value.dtype == np.dtype("=f4"):
                dtype = 1
            elif value.dtype == np.dtype("<f8") or value.dtype == np.dtype("=f8"):
                dtype = 2
            else:
                raise ValueError(f"unsupported dtype for {name}: {value.dtype}")
            encoded = name.encode("utf-8")
            if len(encoded) > 512 or value.ndim == 0 or value.ndim > 8:
                raise ValueError(f"invalid tensor metadata for {name}")
            f.write(struct.pack("<HBBI", len(encoded), value.ndim, dtype, 0))
            f.write(encoded)
            f.write(struct.pack("<" + "I" * value.ndim, *value.shape))
            raw = value.tobytes(order="C")
            f.write(struct.pack("<Q", len(raw)))
            f.write(raw)
    source_manifest = json.loads((source / "manifest.json").read_text(encoding="utf-8")) if (source / "manifest.json").exists() else {}
    manifest = {
        "schema": "abcurves.native.model.v1",
        "format": {"magic": MAGIC.decode("ascii"), "version": VERSION,
                    "endianness": "little", "neural_dtype": "float32",
                    "geometry_dtype": "float64"},
        "variant": f"continuous-{commit}ms",
        "commitment_ms": commit,
        "planner_config": source_manifest.get("reference", {}).get("motor_config", {"commit": commit}),
        "constants": {"common_radians_per_count": 0.0003509487083280618,
                       "history_samples": 640, "warm_history_samples": 160,
                       "sample_us": 1000},
        # Keep the portable manifest independent of the exporting checkout.
        "source_directory": source.name,
        "source_manifest_sha256": sha256(source / "manifest.json") if (source / "manifest.json").exists() else None,
        "tensor_count": len(names),
        "tensors": {name: {"shape": list(arrays[name].shape), "dtype": str(arrays[name].dtype),
                            "sha256": hashlib.sha256(np.ascontiguousarray(arrays[name]).tobytes()).hexdigest()}
                    for name in names},
        "model_sha256": sha256(output),
    }
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return {"variant": commit, "output": str(output), "sha256": manifest["model_sha256"], "tensors": len(names)}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[2] / "Native" / "models")
    parser.add_argument("--output-root", type=Path, default=Path(__file__).resolve().parents[1] / "models")
    parser.add_argument("--variant", type=int, choices=(8, 16, 32), action="append")
    args = parser.parse_args()
    variants = args.variant or [8, 16, 32]
    receipts = []
    for commit in variants:
        source = args.source_root / VARIANTS[commit]
        destination = args.output_root / f"{commit}ms"
        receipts.append(write_bundle(source, destination, commit))
    print(json.dumps(receipts, indent=2))


if __name__ == "__main__":
    main()
