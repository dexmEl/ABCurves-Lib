"""Stage a platform wheel containing the tested Windows x64 ABI DLL, no models."""
import argparse
import datetime
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SETUP = '''from setuptools import setup
from setuptools.command.bdist_wheel import bdist_wheel
class PlatformWheel(bdist_wheel):
    def finalize_options(self):
        super().finalize_options()
        self.root_is_pure = False
    def get_tag(self):
        return "py3", "none", "win_amd64"
setup(cmdclass={"bdist_wheel": PlatformWheel})
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dll', type=Path, default=ROOT / 'build/api/Release/abcurves.dll')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/dist')
    args = parser.parse_args()
    if os.name != 'nt':
        raise SystemExit('Windows packaging requires a tested Windows x64 DLL')
    if not args.dll.is_file():
        raise SystemExit(f'Build the ABI first: {args.dll}')
    stage = Path(tempfile.mkdtemp(prefix='abcurves_wheel_', dir=ROOT / 'build'))
    shutil.copytree(ROOT / 'API/python/src', stage / 'src',
                    ignore=shutil.ignore_patterns('__pycache__', '*.egg-info'))
    shutil.copy2(ROOT / 'API/python/pyproject.toml', stage / 'pyproject.toml')
    shutil.copy2(ROOT / 'API/python/LICENSE', stage / 'LICENSE')
    shutil.copy2(ROOT / 'API/python/README.md', stage / 'README.md')
    native = stage / 'src/abcurves_native/_native'
    native.mkdir(exist_ok=True)
    shutil.copy2(args.dll, native / 'abcurves.dll')
    (stage / 'setup.py').write_text(SETUP)
    args.out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env.setdefault('SOURCE_DATE_EPOCH', '1577836800')
    subprocess.run(['uv', 'build', str(stage), '--wheel', '--out-dir', str(args.out)], check=True, env=env)
    (args.out / 'native_bundle.json').write_text(json.dumps(dict(
        dll_sha256=hashlib.sha256(args.dll.read_bytes()).hexdigest(),
        created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        models_embedded=True), indent=2))


if __name__ == '__main__':
    main()
