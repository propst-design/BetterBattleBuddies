"""Build a fresh stripped BBB Workshop candidate, with source/file provenance."""
from pathlib import Path
import argparse, hashlib, json, subprocess, zipfile

root = Path(__file__).resolve().parent.parent
sha = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
if subprocess.check_output(['git', '-C', str(root), 'status', '--porcelain'], text=True).strip():
    raise SystemExit('Commit or stash BBB changes first.')
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, default=root.parent.parent / '_BBB-release')
options = parser.parse_args()
release_root = options.output.resolve()
if release_root == root.parent or root.parent in release_root.parents:
    raise SystemExit('Stage outside Workbench/addons to avoid duplicate resource GUIDs.')
for ancestor in (release_root, *release_root.parents):
    if ancestor.is_symlink() or ancestor.is_junction():
        raise SystemExit('Refusing a linked staging path.')
parent = release_root / sha[:12]
addon = parent / 'BetterBattleBuddies'
if parent.exists():
    raise SystemExit('Candidate exists; retain it and use a new source commit.')
addon.mkdir(parents=True)
files = []
if not (root / 'resourceDatabase.rdb').is_file():
    raise SystemExit('Open addon.gproj in Workbench to register resource GUIDs first.')
for relative in ['addon.gproj', 'resourceDatabase.rdb', 'LICENSE.md', 'README.md'] + [
        p.relative_to(root).as_posix() for folder in ['Scripts/Game/BBB', 'Configs/BBB', 'UI/layouts/BBB']
        for p in sorted((root / folder).rglob('*')) if p.is_file()]:
    source = root / relative
    target = addon / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    payload = source.read_bytes()
    if relative == 'addon.gproj':
        payload = payload.replace(b'Better Battle Buddies - Development', b'Better Battle Buddies')
    target.write_bytes(payload)
    files.append({'path': relative, 'sha256': hashlib.sha256(payload).hexdigest()})
(parent / 'WORKSHOP-BUILD.json').write_text(json.dumps({
    'commit': sha, 'sourceRoot': str(root), 'addonRoot': str(addon), 'files': files}, indent=2) + '\n')
(parent / 'WORKSHOP-DESCRIPTION.txt').write_bytes((root / 'docs/workshop-description.md').read_bytes())
with zipfile.ZipFile(parent / 'BetterBattleBuddies.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    for item in files:
        archive.write(addon / item['path'], 'BetterBattleBuddies/' + item['path'])
print(addon)
print(f'{len(files)} files; source {sha}; nothing uploaded.')
