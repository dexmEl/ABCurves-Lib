"""Stage explicit source/SDK manifests without deleting local resources.

No network, Git index mutation, publication or license grant is performed here.
"""
from __future__ import annotations
import argparse
import fnmatch
import hashlib
import json
import os
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def configuration():
    return json.loads((ROOT / 'packaging/publication.json').read_text(encoding='utf-8'))


def matches(path, patterns):
    return any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns)


def source_files(include_restricted=False):
    config = configuration()
    paths = []
    for directory, dirs, names in os.walk(ROOT):
        dirs[:] = sorted(d for d in dirs if d not in config['exclude_parts'] and not d.endswith('.egg-info'))
        paths.extend(Path(directory) / name for name in sorted(names))
    for path in paths:
        # Paths under local/generated directories are never candidates, even
        # when an overly broad include pattern is accidentally introduced.
        relative = path.relative_to(ROOT)
        if any(part in config['exclude_parts'] for part in relative.parts):
            continue
        name = relative.as_posix()
        if not path.is_file() or path.is_symlink():
            continue
        if not matches(name, config['include']) or matches(name, config['exclude']):
            continue
        if not include_restricted and matches(name, config['restricted_assets']):
            continue
        yield path, relative


def stage_source(destination, include_restricted):
    return list(source_files(include_restricted))


def stage_runtime(dll):
    if not dll.is_file():
        raise SystemExit('Pass --dll with the path to a built Windows x64 abcurves.dll.')
    files = [(dll, Path('abcurves.dll'))]
    names = ('LICENSE', 'NOTICE', 'packaging/runtime-readme.md',
             'API/c/include/abcurves.h', 'API/c/examples/dll.c',
             'API/cpp/include/abcurves/dll.hpp', 'API/cpp/examples/dll.cpp', 'API/cpp/examples/dll_direct.cpp',
             'API/python/src/abcurves_native/__init__.py', 'API/python/src/abcurves_native/NOTICE',
             'API/python/pyproject.toml', 'API/python/README.md', 'API/python/LICENSE',
             'API/rust/Cargo.toml', 'API/rust/abcurves/Cargo.toml',
             'API/rust/abcurves/src/lib.rs', 'API/rust/abcurves/examples/embedded.rs',
             'API/rust/abcurves-sys/Cargo.toml', 'API/rust/abcurves-sys/build.rs',
             'API/rust/abcurves-sys/src/lib.rs')
    for name in names:
        target = Path('README.md') if name == 'packaging/runtime-readme.md' else Path(name)
        files.append((ROOT / name, target))
    for name in configuration()['sdk']['documents']:
        files.append((ROOT / name, Path(name)))
    for name in ('docs/quick-start.md', 'docs/getting-started/building.md',
                 'docs/getting-started/c.md', 'docs/getting-started/cpp.md',
                 'docs/getting-started/python.md', 'docs/getting-started/rust.md',
                 'docs/api/language-parity.md'):
        files.append((ROOT / name, Path(name)))
    files.append((ROOT / 'examples/README.md', Path('examples/README.md')))
    # Keep included links local and point source-only references to the repository.
    import re
    from urllib.parse import unquote
    config = configuration()
    included = {source.resolve(): target for source, target in files}
    generated = ROOT / 'build/runtime-documentation'
    for index, (source, target) in enumerate(files):
        if source.suffix.lower() != '.md':
            continue
        def link(match):
            destination = match[2].strip('<>')
            if destination.startswith('#') or re.match(r'\w+:', destination):
                return match[0]
            path, _, anchor = destination.partition('#')
            resolved = (source.parent / unquote(path)).resolve()
            if not resolved.is_relative_to(ROOT) or not resolved.exists():
                return match[0]
            if resolved in included:
                new = Path(os.path.relpath(included[resolved], target.parent)).as_posix()
            else:
                new = config['repository_tree'] + resolved.relative_to(ROOT).as_posix()
            return '[' + match[1] + '](' + new + ('#' + anchor if anchor else '') + ')'
        body = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', link, source.read_text(encoding='utf-8'))
        output = generated / target
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(body, encoding='utf-8')
        files[index] = (output, target)
    return files


def stage_sdk(install, models, include_renderer=False):
    if not (install / 'include/abcurves.h').is_file():
        raise SystemExit('Run cmake --install first; --install must name its prefix.')
    files = [(p, p.relative_to(install)) for p in sorted(install.rglob('*')) if p.is_file()]
    for name in ('LICENSE', 'NOTICE'):
        files.append((ROOT / name, Path(name)))
    files.append((ROOT / 'packaging/sdk-readme.md', Path('README.md')))
    for name in ('API/c/examples/segments.c', 'API/cpp/examples/segments.cpp'):
        files.append((ROOT / name, Path(name)))
    # Package user guides, not generated ABI inventories or maintainer receipts.
    import re
    from urllib.parse import unquote
    config = configuration()
    generated = ROOT / 'build/sdk-documentation'
    generated.mkdir(parents=True, exist_ok=True)
    readme = generated / 'README.md'
    readme.write_text((ROOT / 'packaging/sdk-readme.md').read_text(encoding='utf-8').replace('](../docs/', '](docs/'), encoding='utf-8')
    files = [(readme if relative == Path('README.md') else source, relative) for source, relative in files]
    documents = config['sdk']['documents']
    included = {source.resolve(): target for source, target in files}
    included.update({(ROOT / name).resolve(): Path(name) for name in documents})
    included[(ROOT / 'API/c/include/abcurves.h').resolve()] = Path('include/abcurves.h')
    for header in (ROOT / 'lib/include/abcurves').glob('*.hpp'):
        included[header.resolve()] = Path('include/abcurves') / header.name
    for name in documents:
        original = ROOT / name
        def link(match):
            target = match[2].strip('<>')
            if target.startswith('#') or re.match(r'\w+:', target):
                return match[0]
            path, _, anchor = target.partition('#')
            resolved = (original.parent / unquote(path)).resolve()
            assert resolved.exists() and resolved.is_relative_to(ROOT), (name, target)
            if resolved in included:
                new = Path(os.path.relpath(included[resolved], Path(name).parent)).as_posix()
            else:
                new = config['repository_tree'] + resolved.relative_to(ROOT).as_posix()
            return '[' + match[1] + '](' + new + ('#' + anchor if anchor else '') + ')'
        body = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', link, original.read_text(encoding='utf-8'))
        body = body.replace('lib/models/', 'models/')
        target = generated / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(body, encoding='utf-8')
        files.append((target, Path(name)))
    for variant in sorted(set(models)):
        for name in ('manifest.json', 'model.abcmodel'):
            files.append((ROOT / f'lib/models/{variant}ms' / name, Path(f'models/{variant}ms') / name))
    if include_renderer:
        files.append((ROOT / 'Native/models/renderer_global_h80.bin', Path('models/renderer_global_h80.bin')))
    # An SDK consumer project uses installed targets, with no repository paths.
    for p in (ROOT / 'examples/installed').rglob('*'):
        if p.is_file():
            files.append((p, p.relative_to(ROOT)))
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kind', choices=('source', 'sdk', 'runtime'))
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--install', type=Path, default=ROOT / 'build/install')
    parser.add_argument('--dll', type=Path, default=ROOT / 'build/api/Release/abcurves.dll')
    parser.add_argument('--model', type=int, choices=(32,16,8), action='append', help='SDK planner variant; repeat to include several. Default: 16 ms.')
    parser.add_argument('--renderer', action='store_true', help='Include the optional Renderer model image in the SDK.')
    parser.add_argument('--local-validation-assets', action='store_true',
                        help='Compatibility option: include restricted assets if policy defines any; currently all model assets are MIT-covered.')
    args = parser.parse_args()
    destination = args.out.resolve()
    # Staging never overwrites/deletes a previous package.
    if destination.exists() and any(destination.iterdir()):
        raise SystemExit('Output must be absent or empty; choose a new staging directory.')
    if destination == ROOT or not destination.is_relative_to(ROOT / 'build'):
        raise SystemExit('Staging output must be a child of the repository build/ directory.')
    selected_models = args.model or configuration()['sdk']['default_models']
    if args.kind == 'source':
        files = stage_source(destination, args.local_validation_assets)
    elif args.kind == 'runtime':
        files = stage_runtime(args.dll.resolve())
    else:
        files = stage_sdk(args.install.resolve(), selected_models, args.renderer)
    destination.mkdir(parents=True, exist_ok=True)
    entries = []
    seen = set()
    for source, relative in files:
        if relative.as_posix() in seen:
            raise RuntimeError(f'Duplicate package path: {relative}')
        seen.add(relative.as_posix())
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        entries.append(dict(path=relative.as_posix(), bytes=target.stat().st_size, sha256=sha256(target)))
    config = configuration()
    local_only = bool(args.kind == 'source' and args.local_validation_assets and config['restricted_assets'])
    manifest = dict(schema=1, kind=args.kind, abi=config['abi'], local_validation_only=local_only,
                    models=sorted(set(selected_models)) if args.kind=='sdk' else [32,16,8],
                    publication_blockers=config.get('publication_blockers', []),
                    license_coverage=config['license_coverage'],
                    files=entries)
    (destination / 'PUBLICATION_MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(f'Staged {len(entries)} files ({sum(x["bytes"] for x in entries) / 1048576:.1f} MiB) in {destination}')
    if local_only:
        print('LOCAL VALIDATION ONLY: restricted assets included; do not publish this stage.')


if __name__ == '__main__':
    main()
