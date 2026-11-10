"""Measure DLL model loading and Python binding boundaries on the local machine."""
import argparse
import ctypes
import statistics
import time
from pathlib import Path

from abcurves_native import Library, Model, Planner, SegmentController, State

ROOT = Path(__file__).resolve().parents[1]


def median_us(operation, iterations):
    values = []
    for _ in range(iterations):
        start = time.perf_counter_ns()
        operation()
        values.append((time.perf_counter_ns() - start) / 1000)
    return statistics.median(values)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', required=True)
    args = parser.parse_args()
    library = Library(args.dll)
    for variant in (8, 16, 32):
        def load(embedded):
            model = (Model.embedded(variant, library=library) if embedded else
                     Model(ROOT / 'lib/models' / f'{variant}ms', library=library))
            model.close()
        load(False)
        load(True)
        print(f'{variant} ms load and close: external {median_us(lambda: load(False), 30):.2f} us; '
              f'embedded {median_us(lambda: load(True), 30):.2f} us')
    with Model.embedded(16, library=library) as model, Planner(model) as planner:
        with SegmentController(planner) as controller:
            controller.relative((120, -7), 0, 0)
            for _ in range(20):
                controller.next_segment()
            print(f'16 ms Python owned segment: {median_us(controller.next_segment, 1000):.2f} us')
        state = State()
        state.struct_size = ctypes.sizeof(state)
        def raw():
            status = library.raw.abc_planner_get_state(planner._handle, ctypes.byref(state))
            if status:
                raise RuntimeError(status)
        print(f'planner state through ctypes: {median_us(raw, 10000):.3f} us; '
              f'Python property: {median_us(lambda: planner.state, 10000):.3f} us')


if __name__ == '__main__':
    main()
