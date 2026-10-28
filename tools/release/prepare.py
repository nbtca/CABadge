"""Verify and package existing CABadge deliverables; never access a device."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def payload(directory, name):
    path = (directory / name).resolve()
    if not path.is_relative_to(directory.resolve()):
        raise ValueError(f'Path escapes outside release directory: {name}')
    if not path.is_file():
        raise ValueError(f'Missing release file: {name}')
    return path.read_bytes()


def verify_enclosure(directory):
    directory = Path(directory).resolve()
    manifest = read_json(directory / 'MANIFEST.json')
    listed = set()
    for item in manifest['files']:
        raw = payload(directory, item['path'])
        if len(raw) != item['bytes'] or digest(raw) != item['sha256']:
            raise ValueError(f'Manifest size/hash mismatch: {item["path"]}')
        if item['path'] in listed:
            raise ValueError(f'Duplicate manifest path: {item["path"]}')
        listed.add(item['path'])
    actual = {p.relative_to(directory).as_posix() for p in directory.rglob('*') if p.is_file() and p.relative_to(directory).as_posix() not in ['MANIFEST.json', 'SHA256SUMS.txt']}
    if actual != listed:
        raise ValueError('Manifest does not cover the complete release payload')
    sums = set()
    for line in payload(directory, 'SHA256SUMS.txt').decode('utf-8-sig').splitlines():
        expected, name = line.split(maxsplit=1)
        name = name.removeprefix('*')
        if digest(payload(directory, name)) != expected:
            raise ValueError(f'SHA256SUMS mismatch: {name}')
        if name in sums:
            raise ValueError(f'Duplicate SHA256SUMS path: {name}')
        sums.add(name)
    if sums != listed | {'MANIFEST.json'}:
        raise ValueError('SHA256SUMS does not cover the complete release')
    result = verify_enclosure_revision(directory, manifest)
    result['payload_files'] = len(listed)
    return result


def verify_enclosure_revision(directory, manifest):
    if manifest.get('button_revision') != 'R2':
        raise ValueError('Expected button revision R2')
    proof = read_json(directory / 'verification/button-r2.json')
    expected = proof['input_sha256']
    if digest(payload(directory, 'print/V11_Button_R2.stl')) != expected['V11_Button_R2.stl']:
        raise ValueError('R2 STL fingerprint differs from approved verification')
    if (directory / 'print/V11_Button.stl').exists():
        raise ValueError('Old standalone button is still present')
    for kind, part, label, count in [
        ('SolidWorks', 'V11_Button_R2.SLDPRT', 'native', 9),
        ('STEP', 'V11_Button_R2.step', 'STEP', 4),
    ]:
        if kind == 'SolidWorks':
            required = {'CABadge_V11_V2_1.SLDASM', 'V11_Button_R2.SLDPRT'}
            required.update(f'V11_{name}.SLDPRT' for name in ['Cup', 'Keeper', 'Screen', 'Battery', 'Insulator', 'BackLiner', 'PCB_reference'])
        else:
            required = {'CABadge_V11_V2_1_Assembly.step', 'V11_Button_R2.step', 'V11_Cup.step', 'V11_Keeper.step'}
        try:
            with zipfile.ZipFile(directory / f'cad/CABadge_V11_V2_1-{kind}.zip') as archive:
                names = archive.namelist()
                if len(names) != count or set(names) != required or archive.testzip() is not None:
                    raise ValueError(f'Incomplete {kind} archive')
                if any('V11_Button.' in name for name in names):
                    raise ValueError(f'Old button remains in {kind} archive')
                if digest(archive.read(part)) != expected[part]:
                    raise ValueError(f'R2 {label} fingerprint differs from approved verification')
                assembly = 'CABadge_V11_V2_1.SLDASM' if kind == 'SolidWorks' else 'CABadge_V11_V2_1_Assembly.step'
                if assembly not in names:
                    raise ValueError(f'Missing {kind} assembly')
        except (zipfile.BadZipFile, KeyError) as exc:
            raise ValueError(f'Invalid {kind} archive: {exc}') from exc
    return {'kind': 'enclosure', 'button_revision': 'R2', 'status': proof['status'], 'physical_acceptance': proof['physical_acceptance']}


def refresh_manifest(directory):
    directory = Path(directory).resolve()
    manifest = read_json(directory / 'MANIFEST.json')
    verify_enclosure_revision(directory, manifest)
    files = []
    for path in sorted(directory.rglob('*')):
        if path.is_file() and path.relative_to(directory).as_posix() not in ['MANIFEST.json', 'SHA256SUMS.txt']:
            name = path.relative_to(directory).as_posix()
            raw = payload(directory, name)
            files.append({'path': name, 'bytes': len(raw), 'sha256': digest(raw)})
    manifest['files'] = files
    raw = (json.dumps(manifest, ensure_ascii=False, indent=2) + '\n').encode('utf-8')
    sums = ''.join(f"{item['sha256']}  {item['path']}\n" for item in files)
    sums += digest(raw) + '  MANIFEST.json\n'
    (directory / 'MANIFEST.json').write_bytes(raw)
    (directory / 'SHA256SUMS.txt').write_text(sums, encoding='utf-8')
    return verify_enclosure(directory)


def verify_firmware(directory, source_root=ROOT, source_ref='HEAD'):
    directory, source_root = Path(directory).resolve(), Path(source_root).resolve()
    meta = read_json(directory / 'verification.json')
    commit = subprocess.check_output(['git', 'rev-parse', source_ref + '^{commit}'], cwd=source_root).decode().strip()
    if meta.get('source_commit') != commit:
        raise ValueError('Firmware source commit differs from the selected source ref')
    if meta.get('source_dirty') is not False:
        raise ValueError('Cannot publish a firmware build from dirty source')
    if subprocess.check_output(['git', 'status', '--porcelain'], cwd=source_root):
        raise ValueError('Source worktree is dirty; commit before preparing a release')
    config = json.loads(subprocess.check_output(['git', 'show', commit + ':firmware-v7/version.json'], cwd=source_root))
    variant = meta.get('build_variant', '')
    if variant and not re.fullmatch(r'[a-zA-Z0-9][a-zA-Z0-9_-]{0,47}', variant):
        raise ValueError('Invalid firmware variant')
    version = config['version'] + ('-' + variant if variant else '')
    if len(version.encode('ascii')) > 31:
        raise ValueError('Firmware version exceeds the ESP-IDF app-description field')
    if meta.get('firmware_version') != version:
        raise ValueError('Firmware version differs from the selected source version/variant')
    inputs = meta.get('source_inputs', {})
    if 'firmware-v7/version.json' not in inputs:
        raise ValueError('Missing source input fingerprints')
    # Vendor files are downloaded inputs, separately fingerprinted by the build.
    tracked_inputs = [name for name in inputs if not name.startswith('firmware-v7/vendor/lvgl-9.4.0/')]
    queries = ''.join(commit + ':' + name + '\n' for name in tracked_inputs).encode('utf-8')
    objects = subprocess.check_output(['git', 'cat-file', '--batch'], input=queries, cwd=source_root)
    offset = 0
    for name in tracked_inputs:
        end = objects.index(b'\n', offset)
        header = objects[offset:end].split()
        if len(header) != 3 or header[1] != b'blob':
            raise ValueError(f'Source input is not in the selected commit: {name}')
        size = int(header[2])
        raw = objects[end + 1:end + 1 + size]
        if digest(raw) != inputs[name]:
            raise ValueError(f'Source input fingerprint differs from commit: {name}')
        offset = end + size + 2
    for name in ['bootloader.bin', 'partitions.bin', 'firmware.bin', 'firmware.elf']:
        if digest(payload(directory, name)) != meta['hashes'].get(name):
            raise ValueError(f'Firmware artifact hash mismatch: {name}')
    return {'kind': 'firmware', 'source_commit': commit, 'firmware_version': version, 'hardware_test': meta.get('hardware_test', 'PENDING')}


def export_package(directory, output, kind, source_root=ROOT, source_ref='HEAD'):
    directory, output = Path(directory).resolve(), Path(output).resolve()
    if output.is_relative_to(directory):
        raise ValueError('Output must not be inside the source release directory')
    receipt = verify_enclosure(directory) if kind == 'enclosure' else verify_firmware(directory, source_root, source_ref)
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists() or output.with_suffix(output.suffix + '.sha256').exists():
        raise ValueError('Output already exists; choose a new package filename')
    with zipfile.ZipFile(output, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in sorted(directory.rglob('*')):
            if path.is_file():
                name = directory.name + '/' + path.relative_to(directory).as_posix()
                info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                archive.writestr(info, payload(directory, path.relative_to(directory).as_posix()))
    receipt.update(package=output.name, sha256=digest(output.read_bytes()))
    output.with_suffix(output.suffix + '.sha256').write_text(receipt['sha256'] + '  ' + output.name + '\n', encoding='utf-8')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kind', choices=['enclosure', 'firmware'])
    parser.add_argument('directory', type=Path)
    parser.add_argument('--source-ref', default='HEAD', help='Firmware source commit/tag; default HEAD')
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument('--check', action='store_true')
    action.add_argument('--refresh-manifest', action='store_true', help='Enclosure only: verify R2, then regenerate checksums')
    action.add_argument('--output', type=Path, help='New delivery ZIP outside the input directory')
    args = parser.parse_args()
    try:
        if args.refresh_manifest:
            if args.kind != 'enclosure':
                raise ValueError('--refresh-manifest is only for the V11 enclosure')
            result = refresh_manifest(args.directory)
        elif args.output:
            result = export_package(args.directory, args.output, args.kind, source_ref=args.source_ref)
        else:
            result = verify_enclosure(args.directory) if args.kind == 'enclosure' else verify_firmware(args.directory, source_ref=args.source_ref)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f'Release check failed: {exc}\n')
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
