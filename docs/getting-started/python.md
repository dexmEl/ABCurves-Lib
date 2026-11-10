# Python

## Requirements and installation

Python >=3.10 and the Windows x64 native DLL are qualified. The binding uses ctypes and needs no NumPy, Numba or Torch at runtime. A locally built Windows wheel includes the DLL and its embedded models:

```powershell
python -m pip install .\abcurves_native-1.5.0-py3-none-win_amd64.whl
```

No PyPI release is claimed. To build a wheel, see [packaging](../../packaging/README.md). For an external DLL, install the binding from source and specify its path:

Use a matching binary DLL or [build it from source](building.md). From the source repository root:

```powershell
python -m venv .venv
.venv/Scripts/python -m pip install -e API/python
$env:ABCURVES_LIBRARY = "$pwd/build/api/Release/abcurves.dll"
```

Set `ABCURVES_LIBRARY` to the DLL's full path. Its embedded models need no external files. The DLL needs the matching Microsoft Visual C++ runtime. `Model(path)` remains available for external bundles.

## Load, submit and generate

Run this from the repository root with the environment's Python:

```python
from abcurves_native import Model, Planner, SegmentController

with Model.embedded(16) as model, Planner(model) as planner:
    with SegmentController(planner) as controller:
        controller.relative((120.0, -7.0), 0, 0)
        segment = controller.next_segment()
        for sample in segment.samples:
            print(sample.timestamp_us, sample.displacement.x, sample.displacement.y)
```

The target is angular error in common counts; the two zeros are simulated capture/receipt microseconds. Each displacement covers one millisecond. The application converts and schedules the output. Context managers clean up the controller, planner and model.

Run the [existing example](../../API/python/examples/segments.py) with:

```powershell
.venv/Scripts/python API/python/examples/segments.py lib/models/16ms
```

Continue with [integration](../integration.md), [calibration](../guides/calibration.md), and [errors](../advanced/diagnostics.md). Optional [reusable buffers/NumPy views](../advanced/state-and-memory.md) and [feature examples](../../examples/README.md#advanced-examples) are available when needed. The separate original `abcurves` package in `Native/` is for reference work, not runtime use of this binding.
