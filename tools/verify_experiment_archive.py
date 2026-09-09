#!/usr/bin/env python3
"""Read-only verification of preserved Git snapshots and optional local evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

REPO = Path(__file__).resolve().parents[1]
META = REPO / 'docs/reproducibility'


def git(*args, **kwargs):
    return subprocess.check_output(['git', '-C', str(REPO), *args], **kwargs)


def file_sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def verify_sources():
    versions = {x['folder']: x for x in json.loads((META / 'source_inventory.json').read_text())}
    refs = json.loads((META / 'snapshot_refs.json').read_text())
    trees = {}
    objects = set()
    for rec in refs:
        tree = {}
        for item in git('ls-tree', '-rz', rec['commit']).split(b'\0')[:-1]:
            meta, path = item.split(b'\t', 1)
            mode, kind, oid = meta.decode().split()
            if kind != 'blob':
                raise ValueError('unexpected non-file entry: ' + path.decode())
            tree[path.decode()] = (mode, oid)
            objects.add(oid)
        trees[rec['folder']] = tree
    packed = git('cat-file', '--batch', input=('\n'.join(sorted(objects)) + '\n').encode())
    hashes = {}
    offset = 0
    while offset < len(packed):
        end = packed.index(b'\n', offset)
        oid, kind, size = packed[offset:end].decode().split()
        size = int(size)
        data = packed[end + 1:end + 1 + size]
        if kind != 'blob' or len(data) != size:
            raise ValueError('invalid Git object response')
        hashes[oid] = hashlib.sha256(data).hexdigest()
        offset = end + 2 + size
    total = 0
    for name, tree in trees.items():
        expected = versions[name]['files']
        if set(tree) != {x['path'] for x in expected}:
            raise ValueError('snapshot file set differs: ' + name)
        for entry in expected:
            mode, oid = tree[entry['path']]
            if mode != entry['mode'] or hashes[oid] != entry['sha256']:
                raise ValueError('snapshot mismatch: ' + name + '/' + entry['path'])
            total += 1
    return {'snapshots': len(trees), 'file_versions': total, 'status': 'PASS'}


def verify_local(workspace):
    workspace = workspace.resolve()
    missing, changed = [], []
    entries = json.loads((META / 'local_artifacts.json').read_text())
    for entry in entries:
        path = (workspace / entry['path']).resolve()
        if not path.is_relative_to(workspace):
            raise ValueError('artifact path escapes workspace')
        if not path.is_file():
            missing.append(entry['path'])
        elif path.stat().st_size != entry['bytes'] or file_sha(path) != entry['sha256']:
            changed.append(entry['path'])
    return {'files': len(entries), 'missing': missing, 'changed': changed,
            'status': 'FAIL' if missing or changed else 'PASS'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, help='also verify the original local artifact store')
    args = parser.parse_args()
    report = {'git_sources': verify_sources(), 'rf_performed': False}
    if args.workspace:
        report['local_artifacts'] = verify_local(args.workspace)
    print(json.dumps(report, indent=2))
    return int(any(isinstance(v, dict) and v.get('status') != 'PASS' for v in report.values()))


if __name__ == '__main__':
    raise SystemExit(main())
