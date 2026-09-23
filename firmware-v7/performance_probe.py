"""Board rendering / physical LCD probe. USB transports results, never timed pixels."""
from pathlib import Path
import datetime
import json
import math
import struct
import sys
import threading
import time
import serial

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'usb_screen'))
from viewer import packet, Decoder

LATEST = ROOT / 'build' / 'probe-latest.json'
SCENES = ('Carousel', '抽屉', '蓝色名片', '静态壁纸', '菜单切换', 'Wi-Fi 扫描期间 Carousel')


def check_ready(data, lcd=False):
    if len(data) != 12 or data[0] != 1 or data[1] != 16 or struct.unpack_from('<HH', data, 2) != (360, 360) or struct.unpack_from('<I', data, 8)[0] != 0xc7c59dff:
        raise ValueError('设备协议或 PCB 不匹配')
    if lcd and not struct.unpack_from('<H', data, 6)[0] & 8:
        raise ValueError('实屏采样需要 7.1.2-lcd 或更新固件')
    if not struct.unpack_from('<H', data, 6)[0] & 4:
        raise ValueError('当前板端固件不支持性能采样，请先安装 7.1.0-preview')


def summarize(rows, lcd=False):
    audit=bool(rows and rows[0].get("audit")); count=rows[0].get("scene_count",3) if audit else 3
    if count not in (1,3,6) or len(rows) != count or [r.get('scene') for r in rows] != list(range(count)):
        raise ValueError('采样场景不完整，未更新校准')
    for row in rows:
        if row.get('schema') != 1 or row.get('pcb') != 'c7c59dff' or row.get('firmware') not in ('7.1.0-preview', '7.1.1-lcd', '7.1.2-lcd', '7.1.3-lcd', '7.1.4-profile', '7.1.5-usb', '7.1.6-memory', '7.1.7-ram', '7.1.8-partial', '7.1.9-async', '7.2.0-monitor', '7.2.1-monitor', '7.2.2-monitor', '7.2.3-monitor','7.3.3-fluid','7.3.4-async','7.3.5-refresh16','7.3.6-anim16','7.3.7-mem','7.3.8-mem','7.3.9-mem','7.3.10-mem','7.4.0-idf61','7.4.1-idf61','7.4.2-idf61','7.4.3-idf61','7.5.0-library','7.5.1-direct','7.5.2-web','7.5.3-http','7.5.4-upload','7.6.0-apps','7.9.0-direct') or row.get('lcd_transfer', False) is not lcd:
            raise ValueError('采样版本不匹配')
        if row['firmware'] == '7.1.0-preview':
            if row.get('lcd_connected') is not False:
                raise ValueError('旧版无屏采样标记无效')
        elif row.get('lcd_connected') is not True or type(row.get('lcd_transfer')) is not bool:
            raise ValueError('缺少显示采样模式标记')
        for key in ('frames', 'samples', 'elapsed_ms', 'render_us_p50', 'render_us_p95', 'flush_bytes_p95', 'render_us_total', 'flush_bytes_total'):
            if type(row.get(key)) is not int or row[key] < (0 if audit else 1):
                raise ValueError('采样字段无效')
        if not (0 if audit else 10) <= row['frames'] == row['samples'] <= 256 or not 3900 <= row['elapsed_ms'] <= (35000 if row.get('manual') else 20000) or row['render_us_p50'] > row['render_us_p95'] or row['render_us_p95'] > 500000 or row['flush_bytes_p95'] > 259200*32:
            raise ValueError('有效帧不足、缓冲已满或数据超界，未更新校准')
        if lcd:
            if row.get('firmware') not in ('7.1.2-lcd', '7.1.3-lcd', '7.1.4-profile', '7.1.5-usb', '7.1.6-memory', '7.1.7-ram', '7.1.8-partial', '7.1.9-async', '7.2.0-monitor', '7.2.1-monitor', '7.2.2-monitor', '7.2.3-monitor','7.3.3-fluid','7.3.4-async','7.3.5-refresh16','7.3.6-anim16','7.3.7-mem','7.3.8-mem','7.3.9-mem','7.3.10-mem','7.4.0-idf61','7.4.1-idf61','7.4.2-idf61','7.4.3-idf61','7.5.0-library','7.5.1-direct','7.5.2-web','7.5.3-http','7.5.4-upload','7.6.0-apps','7.9.0-direct') or row.get('lcd_transfer') is not True or row.get('lcd_qspi_hz') != 40000000:
                raise ValueError('缺少实体输出测量')
            for key in ('flush_us_p95', 'copy_us_p95', 'frame_us_p95', 'frame_gap_ms_max'):
                if type(row.get(key)) is not int or row[key] < 0:
                    raise ValueError('实屏测量字段无效')
            if (row['frames'] and row['flush_us_p95'] <= 0) or row['frame_us_p95'] < row['flush_us_p95']:
                raise ValueError('实屏传输计时无效')
            row['completed_fps'] = round(row.get('lcd_updates',row['frames']) * 1000 / row['elapsed_ms'], 2)
            row['render_fps'] = round(row['frames'] * 1000 / row['elapsed_ms'],2)
            row['panel_scan_hz'] = None
            if audit: row['touch_response_ms'] = row.get('touch_to_flush_us_p95',0)/1000 if row.get('touch_samples') else None
            if audit:
                if row.get('scene_count')!=count or row.get('lcd_updates')!=row['frames'] or len(row.get('frame_bytes',[]))!=row['samples'] or sum(row['frame_bytes'])!=row['flush_bytes_total']:
                    raise ValueError('逐帧传输数据不完整')
        if not lcd:
            row['headless_present_fps'] = round(row['frames'] * 1000 / row['elapsed_ms'], 2)
            # Sequential estimate; real DMA overlap, panel scan and bus gaps are unmeasured.
            row['estimated_qspi40_p95_frame_ms'] = round(max(1000 / 30, row['render_us_p95'] / 1000 + row['flush_bytes_p95'] / 20000), 3)
    return {'schema': 1, 'lcd_transfer': lcd, 'firmware': rows[0]['firmware'], 'pcb': 'c7c59dff',
            'captured_at': datetime.datetime.now().astimezone().isoformat(),
            'measurement': 'Detailed probe fences each frame; render excludes GUI wait only; worker transfer overlaps rendering; use runtime_probe for normal pipeline FPS' if lcd and rows[0]['firmware'] in ('7.3.4-async','7.3.5-refresh16','7.3.6-anim16','7.3.7-mem','7.3.8-mem','7.3.9-mem','7.3.10-mem','7.4.0-idf61','7.4.1-idf61','7.4.2-idf61','7.4.3-idf61','7.5.0-library','7.5.1-direct','7.5.2-web','7.5.3-http','7.5.4-upload','7.6.0-apps','7.9.0-direct') else 'ESP32 render excludes LCD flush; completed SPI frame rate is not panel scan rate' if lcd else 'ESP32 LVGL render wall-time, no LCD transfer; radio state unchanged; not a radio load test',
            'calibration': 'Physical measurement only; PC calibration unchanged' if lcd else 'Worst scene render P95 + current PC dirty bytes / ideal QSPI bandwidth; 30 FPS cap; not LCD FPS',
            'render_ms': max(r['render_us_p95'] for r in rows) / 1000, 'scenes': rows}


def run(port, cancel, progress=lambda message: None, lcd=False, audit=False, manual=False):
    if audit or manual: lcd=True
    count=1 if manual else 6 if audit else 3
    connection = serial.Serial(port=None, baudrate=115200, timeout=.1, write_timeout=1)
    connection.dtr = connection.rts = False
    connection.port = port
    decoder, rows, ready, started = Decoder(), [], False, False
    deadline, hello_at, heartbeat_at = time.monotonic() + 60, 0, 0
    connection.open()
    try:
        while time.monotonic() < deadline:
            if cancel.is_set():
                raise InterruptedError('采样已取消，未更新校准')
            now = time.monotonic()
            if not ready and now - hello_at > .5:
                connection.write(packet(32, b'\x01')); hello_at = now
            if ready and now - heartbeat_at > .5:
                connection.write(packet(34)); heartbeat_at = now
            for kind, data in decoder.feed(connection.read(8192)):
                if kind == 33:
                    check_ready(data, lcd)
                    if (audit or manual) and not struct.unpack_from("<H",data,6)[0]&16:
                        raise ValueError("完整采样需要 7.1.4-profile 或更新诊断固件")
                    if not ready:
                        ready = started = True
                        connection.write(packet(39, bytes([4 if manual else 3 if audit else 2 if lcd else 1])))
                        progress('请在实体屏滑动、开关菜单，采样约8秒…' if manual else '正在采样 Carousel…')
                elif kind == 40 and started:
                    row = json.loads(data)
                    if row.get('error') or row.get('cancelled'):
                        raise RuntimeError('设备采样中断：' + str(row.get('error', 'cancelled')))
                    if row.get('scene') != len(rows):
                        raise ValueError('场景顺序错误')
                    rows.append(row)
                    if len(rows) == count:
                        if row.get('done') is not True:
                            raise ValueError('设备未确认采样完成')
                        started = False
                        result = summarize(rows, lcd) 
                        folder = ROOT.parent / 'outputs' / 'performance'
                        folder.mkdir(parents=True, exist_ok=True)
                        target = folder / (datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '.json')
                        content = json.dumps(result, ensure_ascii=False, indent=2)
                        target.write_text(content, encoding='utf-8')
                        if not lcd:
                            temp = LATEST.with_suffix('.tmp'); temp.write_text(content, encoding='utf-8'); temp.replace(LATEST)
                        return result, target
                    progress('正在采样' + SCENES[len(rows)] + '…')
        raise TimeoutError('采样超时；请检查固件与串口，校准未更新')
    finally:
        if started:
            try:
                connection.write(packet(39, b'\x00')); connection.flush()
            except serial.SerialException:
                pass  # Board restores its UI after the 5-second bridge heartbeat timeout.
        connection.close()


def calibration():
    data = json.loads(LATEST.read_text(encoding='utf-8'))
    verified = summarize(data['scenes'])
    value = data['render_ms']
    if not isinstance(value, (int, float)) or not math.isfinite(value) or value != verified['render_ms']:
        raise ValueError('校准数据无效')
    return value


def self_test():
    good = b'\x01\x10' + struct.pack('<HHHI', 360, 360, 7, 0xc7c59dff)
    physical_ready = b'\x01\x10' + struct.pack('<HHHI', 360, 360, 15, 0xc7c59dff)
    check_ready(good)
    check_ready(physical_ready, lcd=True)
    try:
        check_ready(good, lcd=True)
        raise AssertionError('physical probe accepted without capability')
    except ValueError:
        pass
    try:
        check_ready(good[:6] + b'\x03\x00' + good[8:])
        raise AssertionError('old firmware accepted')
    except ValueError:
        pass
    rows = [dict(schema=1, scene=i, pcb='c7c59dff', firmware='7.1.0-preview', lcd_connected=False,
                 frames=120, samples=120, elapsed_ms=4000, render_us_p50=8000,
                 render_us_p95=10000+i*5000, flush_bytes_p95=259200, render_us_total=960000, flush_bytes_total=31104000) for i in range(3)]
    result = summarize(rows)
    assert result['render_ms'] == 20 and rows[0]['headless_present_fps'] == 30
    assert abs(rows[2]['estimated_qspi40_p95_frame_ms'] - 1000/30) < .001
    lcd_rows = [dict(r, firmware='7.1.1-lcd', lcd_connected=True, lcd_transfer=False) for r in rows]
    assert summarize(lcd_rows)['firmware'] == '7.1.1-lcd'
    missing_mode = [dict(r) for r in lcd_rows]
    del missing_mode[0]['lcd_transfer']
    try:
        summarize(missing_mode)
        raise AssertionError('missing sampling mode accepted')
    except ValueError:
        pass
    lcd_rows[0]['lcd_transfer'] = True
    try:
        summarize(lcd_rows)
        raise AssertionError('physical transfer accepted as headless render')
    except ValueError:
        pass
    rows[1]['samples'] = 10
    try:
        summarize(rows)
        raise AssertionError('truncated sample accepted')
    except ValueError:
        pass
    raw = packet(40, b'{"schema":1}')
    decoder = Decoder(); assert not decoder.feed(raw[:7]); assert decoder.feed(raw[7:]) == [(40, b'{"schema":1}')]
    # Exercise the actual serial loop against packet bytes, with no board or real calibration write.
    import tempfile
    from unittest.mock import patch
    class Wire:
        def __init__(self, lcd=False, **kwargs): self.pending=bytearray(); self.decoder=Decoder(); self.closed=False; self.cancelled=False; self.lcd=lcd
        def open(self): pass
        def close(self): self.closed=True
        def flush(self): pass
        def write(self, data):
            for kind, value in self.decoder.feed(data):
                if kind==32: self.pending.extend(packet(33,physical_ready if self.lcd else good))
                if kind==39 and value==(b'\x02' if self.lcd else b'\x01'):
                    rows[1]['samples']=120
                    for original in rows:
                        row=dict(original)
                        if self.lcd:
                            row.update(firmware='7.1.3-lcd', lcd_connected=True, lcd_transfer=True, lcd_qspi_hz=40000000,
                                       flush_us_p95=16000, copy_us_p95=3000, frame_us_p95=40000, frame_gap_ms_max=45)
                        row['done']=row['scene']==2
                        self.pending.extend(packet(40,json.dumps(row).encode()))
                if kind==39 and value==b'\x00': self.cancelled=True
            return len(data)
        def read(self, count): data=bytes(self.pending[:count]);del self.pending[:count];return data
    with tempfile.TemporaryDirectory(prefix='probe-check-',dir='F:/CABadgeBuild/temp') as temp:
        base=Path(temp).resolve();assert base.is_relative_to(Path('F:/CABadgeBuild/temp').resolve())
        source=base/'source';(source/'build').mkdir(parents=True)
        wire=Wire()
        with patch('serial.Serial',return_value=wire),patch.dict(globals(),ROOT=source,LATEST=source/'build/probe-latest.json'):
            report,path=run('FAKE',threading.Event())
            assert report['render_ms']==20 and path.is_file() and calibration()==20 and wire.closed
            previous=LATEST.read_bytes();wire=Wire();cancel=threading.Event()
            with patch('serial.Serial',return_value=wire):
                try:
                    run('FAKE',cancel,lambda _:cancel.set())
                    raise AssertionError('cancel ignored')
                except InterruptedError: pass
            assert wire.closed and wire.cancelled and LATEST.read_bytes()==previous
            wire=Wire(lcd=True)
            with patch('serial.Serial',return_value=wire):
                report,path=run('FAKE',threading.Event(),lcd=True)
            assert wire.closed and report['lcd_transfer'] and path.is_file()
            assert report['scenes'][0]['completed_fps']==30 and LATEST.read_bytes()==previous
            try:
                summarize(report['scenes'])
                raise AssertionError('physical result accepted for headless calibration')
            except ValueError:
                pass
            report['scenes'][0]['frame_us_p95']=1
            try:
                summarize(report['scenes'],lcd=True)
                raise AssertionError('invalid physical timing accepted')
            except ValueError:
                pass
    audit_rows=[dict(physical_ready=True,schema=1,scene=i,firmware='7.1.4-profile',pcb='c7c59dff',audit=True,scene_count=6,
        lcd_connected=True,lcd_transfer=True,lcd_qspi_hz=40000000,frames=0,samples=0,elapsed_ms=4000,
        render_us_p50=0,render_us_p95=0,render_us_total=0,flush_bytes_p95=0,flush_bytes_total=0,
        flush_us_p95=0,copy_us_p95=0,frame_us_p95=0,frame_gap_ms_max=0,lcd_updates=0,frame_bytes=[]) for i in range(6)]
    assert summarize(audit_rows,True)['scenes'][3]['completed_fps']==0
    audit_rows[0]['frame_bytes']=[1]
    try: summarize(audit_rows,True);raise AssertionError('bad per-frame bytes accepted')
    except ValueError: pass
    print('Probe protocol, percentile calibration inputs, sample integrity: PASS; no serial port opened')


if __name__ == '__main__':
    if '--self-test' in sys.argv:
        self_test()
    else:
        result, target = run(sys.argv[1], threading.Event(), print, lcd='--lcd' in sys.argv, audit='--audit' in sys.argv, manual='--touch' in sys.argv)
        print(target)
