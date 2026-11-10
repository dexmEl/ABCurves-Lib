"""Exact comparisons between the DLL resources and the original asset files."""
import ctypes
import os
from pathlib import Path
import unittest

from abcurves_native import Library, Model, Planner, Renderer, SegmentController, prepare_history

ROOT = Path(__file__).resolve().parents[3]


def image(value):
    return ctypes.string_at(ctypes.addressof(value), ctypes.sizeof(value))


@unittest.skipUnless(os.name == 'nt', 'embedded resources are available in Windows DLL builds')
class EmbeddedParity(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.library = Library(os.environ.get('ABCURVES_LIBRARY'))

    def test_models(self):
        for variant in (8, 16, 32):
            with self.subTest(variant=variant):
                with Model(ROOT / 'lib' / 'models' / f'{variant}ms', library=self.library) as external, \
                     Model.embedded(variant, library=self.library) as embedded:
                    self.assertEqual(external.info.sha256, embedded.info.sha256)
                    for prefix in (None, [(0.05, -0.02)] * 160,
                                   prepare_history([(0.05, -0.02)] * 164,
                                                   current_position=(0, 0), library=self.library).history):
                        with Planner(external, seed=17, history=prefix) as a, \
                             Planner(embedded, seed=17, history=prefix) as b, \
                             SegmentController(a) as ca, SegmentController(b) as cb:
                            for i in range(30):
                                timestamp = i * variant * 1000
                                if i == 9:
                                    for controller in (ca, cb):
                                        controller.unavailable(timestamp - 1000, timestamp)
                                elif i % 3 == 0:
                                    for controller in (ca, cb):
                                        controller.relative((80 - i, 10 + i), max(0, timestamp - (i % 5) * 1000), timestamp)
                                sa, sb = ca.next_segment(), cb.next_segment()
                                self.assertEqual(image(sa.metadata), image(sb.metadata))
                                self.assertEqual(len(sa.samples), len(sb.samples))
                                for x, y in zip(sa.samples, sb.samples):
                                    self.assertEqual(image(x), image(y))

    def test_renderer(self):
        with Renderer(ROOT / 'Native/models/renderer_global_h80.bin', library=self.library) as a, \
             Renderer.embedded(library=self.library) as b:
            profile = [(i % 7 - 3, i % 5 - 2) for i in range(256)]
            for renderer in (a, b):
                renderer.set_profile(profile)
                renderer.reset(42)
            for i in range(1000):
                intent = ((i % 17 - 8) / 7, (i % 13 - 6) / 9)
                self.assertEqual(image(a.step(intent)), image(b.step(intent)))

    def test_invalid_variant(self):
        with self.assertRaises(ValueError):
            Model.embedded(12, library=self.library)
        handle = ctypes.c_uint64(123)
        self.assertEqual(self.library.raw.abc_model_load_embedded(12, ctypes.byref(handle)), 1)
        self.assertEqual(handle.value, 0)
        self.assertEqual(self.library.raw.abc_model_load_embedded(16, None), 3)


if __name__ == '__main__':
    unittest.main()
