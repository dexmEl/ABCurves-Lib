"""Create reproducible Windows release ZIPs from a staged runtime SDK."""
import argparse
import hashlib
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def archive(path, files):
    if path.exists():
        raise SystemExit(f'Refusing to overwrite {path}')
    with zipfile.ZipFile(path, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as out:
        for name, source in sorted(files):
            entry = zipfile.ZipInfo(name, (2020, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            out.writestr(entry, source.read_bytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--wheel', type=Path, help='Include the built wheel in SHA256SUMS.txt')
    args = parser.parse_args()
    stage = args.stage.resolve()
    if not (stage / 'abcurves.dll').is_file():
        parser.error('stage must contain abcurves.dll')
    args.out.mkdir(parents=True, exist_ok=True)
    minimal = [('abcurves.dll', stage / 'abcurves.dll'), ('LICENSE', stage / 'LICENSE'),
               ('NOTICE', stage / 'NOTICE'), ('README.md', ROOT / 'packaging/minimal-readme.md')]
    files = [(p.relative_to(stage).as_posix(), p) for p in stage.rglob('*') if p.is_file() and
             '__pycache__' not in p.parts and 'target' not in p.parts and p.suffix != '.pyc']
    outputs = [args.out / 'abcurves-windows-x64.zip', args.out / 'abcurves-windows-x64-sdk.zip']
    manifest = args.out / 'SHA256SUMS.txt'
    if any(p.exists() for p in [*outputs, manifest]):
        raise SystemExit('Refusing to overwrite existing release files')
    if args.wheel:
        wheel = args.wheel.resolve()
        if not wheel.is_file() or wheel.suffix != '.whl':
            parser.error('--wheel must name an existing wheel')
        wheel_target = args.out / wheel.name
        if wheel_target.exists():
            raise SystemExit(f'Refusing to overwrite {wheel_target}')
    archive(outputs[0], minimal)
    archive(outputs[1], files)
    if args.wheel:
        wheel_target.write_bytes(wheel.read_bytes())
        outputs.append(wheel_target)
    manifest.write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in outputs), encoding='ascii')
    for p in outputs:
        print(p)


if __name__ == '__main__':
    main()
