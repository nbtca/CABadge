"""Repository structure checks only; never connects to a board or builds firmware."""
from pathlib import Path
import ast, hashlib, json, re, subprocess, urllib.parse, zipfile

root = Path(__file__).resolve().parents[1]
files = [Path(p) for p in subprocess.check_output(
    ['git', 'ls-files', '-z'], cwd=root).decode().split('\0') if p]
names = {p.as_posix() for p in files}
errors = []
counts = dict(files=len(files), markdown=0, links=0, python=0, json=0, zip=0)
counts.update(manifest_entries=0, sha256_entries=0)


def check_hash(parent, name, expected, size=None):
    target = (parent / name).resolve()
    key = target.relative_to(root).as_posix()
    if key not in names:
        raise ValueError(f'checksum target not tracked: {key}')
    data = target.read_bytes()
    if ((size is not None and len(data) != size)
            or hashlib.sha256(data).hexdigest() != expected.lower()):
        raise ValueError('size/SHA-256 mismatch')


for rel in files:
    p = root / rel
    try:
        raw = p.read_bytes()
        assert raw, 'empty file'
        if p.suffix == '.py':
            ast.parse(raw.decode('utf-8-sig'), filename=str(rel))
            counts['python'] += 1
        if p.suffix in ('.json', '.kicad_pro', '.gbrjob'):
            json.loads(raw.decode('utf-8-sig'))
            counts['json'] += 1
        if p.suffix in ('.zip', '.epro2', '.epro'):
            with zipfile.ZipFile(p) as archive:
                assert archive.testzip() is None, 'ZIP CRC failure'
            counts['zip'] += 1
        if p.name == 'MANIFEST.json':
            for item in json.loads(raw.decode('utf-8-sig'))['files']:
                try:
                    check_hash(p.parent, item['path'], item['sha256'], item['bytes'])
                    counts['manifest_entries'] += 1
                except Exception as exc:
                    errors.append(f'{rel}: {item["path"]}: {exc}')
        if p.name == 'SHA256SUMS.txt':
            for line in raw.decode('utf-8-sig').splitlines():
                if not line.strip():
                    continue
                try:
                    expected, name = line.split(maxsplit=1)
                    assert re.fullmatch(r'[0-9a-fA-F]{64}', expected), 'invalid SHA-256'
                    check_hash(p.parent, name.removeprefix('*'), expected)
                    counts['sha256_entries'] += 1
                except Exception as exc:
                    errors.append(f'{rel}: {line}: {exc}')
        if p.suffix == '.md':
            counts['markdown'] += 1
            for link in re.findall(r'\[[^\]\n]*\]\(([^)\n]+)\)', raw.decode('utf-8-sig')):
                target = urllib.parse.unquote(link.strip('<>').split('#')[0])
                if not target or re.match(r'[A-Za-z][A-Za-z0-9+.-]*:', target):
                    continue
                key = (p.parent / target).resolve().relative_to(root).as_posix()
                assert key in names or any(n.startswith(key.rstrip('/') + '/') for n in names), link
                counts['links'] += 1
    except Exception as exc:
        errors.append(f'{rel}: {exc}')
# Check the staged firmware source mapping used by usb_screen/build.ps1.
source_dir = root / 'firmware-v7/usb_screen/device/src'
cmake = (source_dir / 'CMakeLists.txt').read_text(encoding='utf-8')
source_list = re.search(r'SRCS (.*?)INCLUDE_DIRS', cmake, re.S)[1]
for name in re.findall(r'"([^"]+)"', source_list):
    candidates = [source_dir / name, root / 'firmware-v7' / name,
                  root / 'firmware-v7/usb_screen' / name]
    if not any(p.is_file() for p in candidates):
        errors.append(f'Missing firmware source: {name}')
for p in [root / 'hardware/JXBadge_V1.kicad_pcb',
          *(root / 'hardware/JXBadge.pretty').glob('*.kicad_mod')]:
    for name in re.findall(r'\(model "\$\{KIPRJMOD\}/([^"\n]+)"', p.read_text(encoding='utf-8')):
        if not (root / 'hardware' / name).is_file():
            errors.append(f'Missing local 3D model: {name}')
print(json.dumps(counts, ensure_ascii=False))
if errors:
    raise SystemExit('\n'.join(errors))
print('PASS: syntax, tracked local Markdown targets, ZIP/native archive CRC, all manifests/SHA-256 lists, firmware sources and local models.')
