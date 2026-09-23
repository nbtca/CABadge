"""Repository structure checks only; never connects to a board or builds firmware."""
from pathlib import Path
import ast, hashlib, json, re, subprocess, urllib.parse, zipfile

root = Path(__file__).resolve().parents[1]
files = [Path(p) for p in subprocess.check_output(
    ['git', 'ls-files', '-z'], cwd=root).decode().split('\0') if p]
names = {p.as_posix() for p in files}
errors = []
counts = dict(files=len(files), markdown=0, links=0, python=0, json=0, zip=0)
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
        if p.suffix == '.zip':
            with zipfile.ZipFile(p) as archive:
                assert archive.testzip() is None, 'ZIP CRC failure'
            counts['zip'] += 1
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
manifest = root / 'hardware/fabrication/JXBadge_EVT_20260909_c7c59dff/MANIFEST.json'
for item in json.loads(manifest.read_text(encoding='utf-8-sig'))['files']:
    p = manifest.parent / item['path']
    data = p.read_bytes()
    if len(data) != item['bytes'] or hashlib.sha256(data).hexdigest() != item['sha256']:
        errors.append(f'Manufacturing manifest mismatch: {p}')
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
print('PASS: syntax, tracked local Markdown targets, ZIP CRC, manufacturing hashes, firmware sources and local models.')
