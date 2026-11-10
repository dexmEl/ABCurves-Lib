"""Check public Markdown, inventory/version coverage, asset hashes and exclusions.

Reports sensitive candidates by filename/line only, never secret values.
"""
from __future__ import annotations
import importlib.util
import json
from pathlib import Path
import re
import sys
from urllib.parse import unquote

from publication import ROOT, configuration, matches, sha256, source_files


def check():
    public = dict(source_files(True))
    publishable = {path.resolve() for path, _ in source_files()}
    errors = []
    candidates = []
    sensitive = re.compile(r'(?:AKIA[0-9A-Z]{16}|gh[pousr]_[A-Za-z0-9]{30,}|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----|[A-Za-z]:[/\\](?:Users|ABCurves)[/\\]|/home/[^/\s]+/|/Users/[^/\s]+/)')
    markdown = 0
    for path, relative in public.items():
        if path.suffix in ('.md', '.json', '.py', '.c', '.cpp', '.hpp', '.h', '.rs', '.toml', '.cmake', '.txt'):
            try:
                text = path.read_text(encoding='utf-8')
            except UnicodeError:
                continue
            # This scanner's own regex is not a credential or local path.
            if relative.as_posix() != 'tools/check_publication.py':
                for line, content in enumerate(text.splitlines(), 1):
                    if sensitive.search(content):
                        candidates.append(dict(path=relative.as_posix(), line=line))
        if path.suffix != '.md':
            continue
        markdown += 1
        if text.count('```') % 2:
            errors.append(f'Unbalanced fences: {relative}')
        prose = re.sub(r'```.*?```', '', text, flags=re.S)
        for target in re.findall(r'\]\(([^)]+)\)', prose):
            target = target.strip('<>').split('#')[0]
            if not target or re.match(r'\w+://|mailto:', target):
                continue
            resolved = (path.parent / unquote(target)).resolve()
            if not resolved.exists():
                errors.append(f'Broken link: {relative} -> {target}')
            elif not resolved.is_relative_to(ROOT):
                errors.append(f'Link escapes repository: {relative}')
            elif path.resolve() in publishable and resolved.is_file() and resolved not in publishable:
                errors.append(f'Link target omitted from software source stage: {relative} -> {target}')
    header = (ROOT / 'API/c/include/abcurves.h').read_text()
    assert re.search(r'#define ABC_ABI_MAJOR 1u', header)
    assert re.search(r'#define ABC_ABI_MINOR 5u', header)
    # Regenerate from tracked headers/bindings; never depend on a private archive.
    spec = importlib.util.spec_from_file_location('audit', ROOT / 'API/tools/audit_application_io.py')
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    generated = ROOT / 'build/api-reference'
    audit.generate(generated)
    inputs = json.loads((generated / 'application_inputs.json').read_text())
    outputs = json.loads((generated / 'application_outputs.json').read_text())
    exports = set(re.findall(r'ABC_CALL\s+(abc_\w+)\s*\(', header))
    assert len(exports) == 55 == len(inputs['c_operations'])
    assert exports == {o['name'] for o in inputs['c_operations']}
    assert all(o['return_type'] in ('abc_status', 'const char *') for o in inputs['c_operations'])
    rust = (ROOT / 'API/rust/abcurves-sys/src/lib.rs').read_text()
    assert exports == set(re.findall(r'pub fn (abc_\w+)\s*\(', rust))
    assets = json.loads((ROOT / 'docs/evidence/model-assets.json').read_text())['assets']
    staged_manifest = ROOT / 'PUBLICATION_MANIFEST.json'
    stage = json.loads(staged_manifest.read_text()) if staged_manifest.is_file() else {}
    public_stage = stage.get('kind') == 'source' and stage.get('local_validation_only') is False
    omitted_assets = []
    for key, receipt in assets.items():
        path = ROOT / key
        if not path.is_file() and public_stage and matches(key, configuration()['restricted_assets']):
            omitted_assets.append(key)
            continue
        assert path.stat().st_size == receipt['bytes'] and sha256(path) == receipt['sha256'], key
    ordinary = {relative.as_posix() for _, relative in source_files()}
    local = {relative.as_posix() for relative in public.values()}
    assert ordinary <= local
    assert not any('.dev-local' in name or '/.venv/' in name or '/target/' in name or name.startswith('build/') for name in local)
    result = dict(markdown_files=markdown, c_exports=len(exports), input_entries=len(inputs['inputs']),
                  output_entries=len(outputs['outputs']), model_assets=len(assets) - len(omitted_assets),
                  intentionally_omitted_restricted_assets=len(omitted_assets),
                  public_source_stage=public_stage,
                  public_source_files=len(ordinary), local_validation_files=len(local),
                  sensitive_candidates=candidates, link_errors=errors,
                  limitation='Filesystem/current-public-source scan only; Git history is not scanned; regex scanning is not a proof that all secrets are absent.')
    out = ROOT / 'build/publication-check.json'
    out.parent.mkdir(exist_ok=True)
    out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))
    return not errors and not candidates


if __name__ == '__main__':
    sys.exit(0 if check() else 1)
