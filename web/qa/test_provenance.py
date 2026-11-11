"""Regression coverage for building without local Python installation metadata."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import native_source_paths, DATA, ROOT, read_json


class SourceProvenanceTests(unittest.TestCase):
    def test_local_metadata_and_untracked_sources_are_excluded(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            subprocess.run(['git', 'init', '-q', str(repo)], check=True)
            tracked = ['lib/src/core/planner.cpp', 'API/python/src/abcurves_native/__init__.py']
            local = ['API/python/src/abcurves_native.egg-info/dependency_links.txt',
                     'API/python/src/abcurves_native/__pycache__/__init__.cpython-311.pyc',
                     'lib/src/local_debug.cpp']
            (repo / '.gitignore').write_text('*.egg-info/\n__pycache__/\n', encoding='utf8')
            for name in tracked + local:
                path = repo / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('local fixture\n', encoding='utf8')
            subprocess.run(['git', 'add', '--', *tracked], cwd=repo, check=True, capture_output=True)
            names = [path.relative_to(repo).as_posix() for path in native_source_paths(repo)]
            self.assertEqual(names, sorted(tracked))

    def test_retained_provenance_covers_only_tracked_native_sources(self):
        native = read_json(DATA / 'comparison.json')['native']
        expected = {path.relative_to(ROOT.parent).as_posix() for path in native_source_paths(ROOT.parent)}
        self.assertEqual(set(native['sources']), expected)
        self.assertEqual(set(native['source_bytes_sha256']), expected)


if __name__ == '__main__':
    unittest.main()
