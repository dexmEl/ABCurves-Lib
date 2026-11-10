"""Exercise a staged Windows runtime package in a temporary isolated directory."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import re
from urllib.parse import unquote


def run(command, *, cwd, env):
    print('+', ' '.join(map(str, command)), flush=True)
    subprocess.run(command, cwd=cwd, env=env, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path, help='Directory staged with publication.py runtime')
    parser.add_argument('--wheel', type=Path, help='Install and test the bundled Windows wheel')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('the runtime package and this check require Windows')
    package = args.package.resolve()
    if not (package / 'abcurves.dll').is_file():
        parser.error('package must contain abcurves.dll')
    forbidden = [p for p in package.rglob('*') if p.is_file() and
                 (p.suffix.lower() in ('.lib', '.abcmodel', '.bin') or
                  (p.suffix.lower() == '.dll' and p != package / 'abcurves.dll'))]
    if forbidden:
        raise SystemExit(f'unexpected native or model files: {forbidden}')
    for doc in package.rglob('*.md'):
        body = re.sub(r'```.*?```', '', doc.read_text(encoding='utf-8'), flags=re.S)
        for target in re.findall(r'\]\(([^)]+)\)', body):
            target = target.strip('<>').split('#')[0]
            if not target or re.match(r'\w+://|mailto:', target):
                continue
            resolved = (doc.parent / unquote(target)).resolve()
            if not resolved.is_relative_to(package) or not resolved.exists():
                raise SystemExit(f'broken staged link: {doc.relative_to(package)} -> {target}')
    with tempfile.TemporaryDirectory(prefix='abcurves-deployment-') as temporary:
        root = Path(temporary)
        isolated = root / 'package'
        shutil.copytree(package, isolated, ignore=shutil.ignore_patterns('__pycache__', 'target', 'Cargo.lock'))
        scratch = root / 'scratch'
        scratch.mkdir()
        env = os.environ.copy()
        env.update(ABCURVES_LIBRARY=str(isolated / 'abcurves.dll'),
                   PYTHONPATH=str(isolated / 'API/python/src'),
                   CARGO_TARGET_DIR=str(root / 'cargo-target'),
                   TEMP=str(scratch), TMP=str(scratch))
        python_check = (
            'from abcurves_native import Model, Planner, SegmentController, Renderer; '
            'models=[Model.embedded(n) for n in (8,16,32)]; '
            'assert [m.info.commitment_ms for m in models]==[8,16,32]; '
            'p=Planner(models[1]); c=SegmentController(p); c.relative((120,-7),0,0); '
            'assert len(c.next_segment().samples)>0; r=Renderer.embedded()'
        )
        run([sys.executable, '-c', python_check], cwd=isolated, env=env)
        if list(scratch.iterdir()):
            raise SystemExit('Python runtime loading unexpectedly wrote temporary files')
        if args.wheel:
            wheel = args.wheel.resolve()
            venv = root / 'venv'
            run([sys.executable, '-m', 'venv', str(venv)], cwd=root, env=env)
            py = venv / 'Scripts/python.exe'
            clean_env = env.copy()
            clean_env.pop('PYTHONPATH', None)
            clean_env.pop('ABCURVES_LIBRARY', None)
            run([str(py), '-m', 'pip', 'install', '--no-deps', str(wheel)], cwd=root, env=clean_env)
            run([str(py), '-c', python_check], cwd=root, env=clean_env)
        project = root / 'project'
        project.mkdir()
        (project / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(abcurves_deployment LANGUAGES C CXX)
add_executable(c_example "${PACKAGE}/API/c/examples/dll.c")
target_include_directories(c_example PRIVATE "${PACKAGE}/API/c/include")
add_executable(cpp_example "${PACKAGE}/API/cpp/examples/dll.cpp")
target_compile_features(cpp_example PRIVATE cxx_std_20)
target_include_directories(cpp_example PRIVATE "${PACKAGE}/API/c/include" "${PACKAGE}/API/cpp/include")
add_executable(cpp_direct_example "${PACKAGE}/API/cpp/examples/dll_direct.cpp")
target_compile_features(cpp_direct_example PRIVATE cxx_std_20)
target_include_directories(cpp_direct_example PRIVATE "${PACKAGE}/API/c/include" "${PACKAGE}/API/cpp/include")
''', encoding='utf-8')
        build = root / 'build'
        run(['cmake', '-S', str(project), '-B', str(build), f'-DPACKAGE={isolated.as_posix()}'], cwd=root, env=env)
        run(['cmake', '--build', str(build), '--config', 'Release'], cwd=root, env=env)
        binaries = build / 'Release'
        missing_env = env.copy()
        missing_env['PATH'] = os.pathsep.join(p for p in env['PATH'].split(os.pathsep)
                                              if 'abcurves' not in p.lower())
        missing = subprocess.run([str(binaries / 'cpp_example.exe')], cwd=root,
                                 env=missing_env, capture_output=True, text=True)
        if missing.returncode == 0 or 'Cannot load abcurves.dll' not in missing.stderr:
            raise SystemExit('C++ missing-DLL check failed')
        shutil.copy2(isolated / 'abcurves.dll', binaries / 'abcurves.dll')
        for name in ('c_example.exe', 'cpp_example.exe', 'cpp_direct_example.exe'):
            run([str(binaries / name)], cwd=binaries, env=env)
        run(['cargo', 'run', '--manifest-path', str(isolated / 'API/rust/Cargo.toml'),
             '-p', 'abcurves-native', '--example', 'embedded'], cwd=isolated, env=env)
    print('Isolated C, C++, Python and Rust runtime checks passed.')


if __name__ == '__main__':
    main()
