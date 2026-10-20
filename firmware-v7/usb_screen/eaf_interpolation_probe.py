"""Real-board LuoXiaohei A/B/A timing; no asset upload or persistent interpolation setting."""
import argparse
import json
from pathlib import Path
import time
from heart_rate_probe import Board


def collect(board, label, seconds):
    samples = []
    deadline = time.monotonic() + seconds
    while True:
        before = time.monotonic()
        state = board.state()
        samples.append({'host_seconds': (before + time.monotonic()) / 2, 'state': state})
        eaf = state['wallpaper']['eaf']
        assert state['wallpaper']['selected'] == 10 and eaf['direct'] and eaf['frames'] == 300
        assert state['wallpaper']['crc'] == 4182038819 and eaf['timing_crc'] == 1640623480
        assert eaf['timing']['errors'] == 0
        if time.monotonic() >= deadline:
            break
        time.sleep(.5)
    first, last = samples[0], samples[-1]
    a, b = first['state']['wallpaper']['eaf']['timing'], last['state']['wallpaper']['eaf']['timing']
    count = b['completed'] - a['completed']
    assert count > 0, 'no completed LCD frames'
    summary = {'completed_delta': count,
               'host_elapsed_seconds': last['host_seconds'] - first['host_seconds'],
               'host_count_fps': count / (last['host_seconds'] - first['host_seconds']),
               'last_timing': b}
    if 'sum_interval_us' in b:
        summary['device_interval_us'] = (b['sum_interval_us'] - a['sum_interval_us']) / count
        summary['device_fps'] = 1e6 / summary['device_interval_us']
    for kind in ('original', 'inserted'):
        if kind not in b:
            continue
        x, y = a[kind], b[kind]
        n = y['completed'] - x['completed']
        summary[kind] = {'completed_delta': n}
        if n:
            for metric in ('prepare', 'source_decode', 'blend', 'staging', 'submit', 'drain', 'work'):
                summary[kind][metric + '_us'] = (y['sum_' + metric + '_us'] - x['sum_' + metric + '_us']) / n
    print(json.dumps({'phase': label, **summary}, ensure_ascii=False), flush=True)
    return {'label': label, 'samples': samples, 'summary': summary}


def lifecycle(board, evidence):
    for label, page in (('menu_pause', 1), ('home_resume', 0)):
        board.exchange(41, bytes((1, page)), 42)
        time.sleep(1)
        a = board.state()
        time.sleep(.6)
        b = board.state()
        evidence[label] = [a, b]
        if page:
            assert not b['wallpaper']['eaf']['playing'] and not b['wallpaper']['eaf']['direct']
            assert a['wallpaper']['eaf']['frame'] == b['wallpaper']['eaf']['frame']
        else:
            assert b['wallpaper']['eaf']['direct'] and b['wallpaper']['eaf']['interpolation']['enabled']
            assert b['wallpaper']['eaf']['timing']['completed'] > a['wallpaper']['eaf']['timing']['completed']
    for asleep in (True, False):
        board.command(10, bytes((asleep,)))
        time.sleep(1)
        a = board.state()
        time.sleep(.6)
        b = board.state()
        evidence['sleep' if asleep else 'wake'] = [a, b]
        assert b['asleep'] == asleep
        assert b['wallpaper']['eaf']['direct'] != asleep
        if asleep:
            assert a['wallpaper']['eaf']['frame'] == b['wallpaper']['eaf']['frame']
        else:
            assert b['wallpaper']['eaf']['timing']['completed'] > a['wallpaper']['eaf']['timing']['completed']
    try:
        board.command(12, b'\x14')
        time.sleep(1)
        evidence['static'] = board.state()
        assert evidence['static']['wallpaper']['selected'] == 20 and not evidence['static']['wallpaper']['eaf']['loaded']
    finally:
        board.command(12, b'\x0a')
    # ACK accepts a selection, not completion of Flash validation/load/prewarm.
    started = time.monotonic()
    evidence['restore_samples'] = []
    while True:
        state = board.state()
        evidence['restore_samples'].append({'elapsed_seconds': time.monotonic() - started, 'state': state})
        eaf = state['wallpaper']['eaf']
        if state['wallpaper']['selected'] == 10 and eaf['direct'] and eaf['interpolation']['enabled']:
            evidence['restored'] = state
            break
        assert time.monotonic() - started < 8, 'dynamic wallpaper did not resume within 8 seconds'
        time.sleep(.25)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM3')
    parser.add_argument('--preflight', action='store_true')
    parser.add_argument('--lifecycle-only', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    evidence = {'phases': [], 'method': 'RGB565 adjacent-frame 50/50 blend; original timeline, one inserted frame per gap'}
    board = Board(args.port)
    try:
        evidence['before'] = board.state()
        assert evidence['before']['product'] == 'CABadge'
        assert evidence['before']['firmware'] == '7.9.18-heart-rate'
        evidence['runtime_before'] = board.runtime()
        if evidence['before']['wallpaper']['selected'] != 10:
            board.command(12, b'\x0a')
        if evidence['before']['asleep']:
            board.command(10, b'\x00')
        if not evidence['before']['wallpaper']['eaf'].get('playing'):
            board.exchange(41, b'\x01\x00', 42)
        time.sleep(2)
        if args.lifecycle_only:
            assert board.state()['wallpaper']['eaf']['interpolation']['enabled']
            evidence['lifecycle'] = {}
            lifecycle(board, evidence['lifecycle'])
        elif args.preflight:
            evidence['phases'].append(collect(board, 'preflash_baseline', 15))
        else:
            assert board.state()['wallpaper']['eaf']['interpolation']['available'], 'wrong firmware build'
            for label, enabled, seconds in (('baseline_a', 0, 15), ('interpolated_b', 1, 30), ('baseline_a2', 0, 15)):
                board.exchange(41, bytes((11, enabled)), 42)
                time.sleep(2)
                assert board.state()['wallpaper']['eaf']['interpolation']['enabled'] == bool(enabled)
                evidence['phases'].append(collect(board, label, seconds))
            board.exchange(41, b'\x0b\x01', 42)  # Leave requested experiment visible; reboot defaults OFF.
            time.sleep(1)
        evidence['after'] = board.state()
        evidence['runtime_after'] = board.runtime()
        for key in ('brightness', 'reduced_motion'):
            assert evidence['before'][key] == evidence['after'][key]
        for key in ('items', 'crcs', 'timing_crcs'):
            assert evidence['before']['wallpaper'][key] == evidence['after']['wallpaper'][key]
        evidence['passed'] = True
    except Exception as exc:
        evidence['error'] = repr(exc)
        evidence['failure_state'] = board.state()
        raise
    finally:
        board.wire.close()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
