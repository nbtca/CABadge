"""Real TimeTiles lifecycle, timers, six-alarm configuration, visual alerts and reboot checkpoints."""
import argparse
from datetime import datetime,timezone,timedelta
import json
from pathlib import Path
import sys
import struct
import subprocess
import time
from heart_rate_probe import Board


def settled(board,predicate,seconds=8):
    deadline=time.monotonic()+seconds
    while True:
        state=board.state()
        if predicate(state):return state
        if time.monotonic()>=deadline:raise AssertionError('board did not settle: '+str(state.get('timetiles')))
        time.sleep(.2)


def command(board,op,data=b''):
    board.exchange(41,bytes((13,op))+data,42)


def connect(port):
    deadline=time.monotonic()+15
    while True:
        try:return Board(port)
        except (OSError,TimeoutError):
            if time.monotonic()>=deadline:raise
            time.sleep(.3)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',default='COM3')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();evidence={'themes':[],'reopen':[]};board=connect(args.port);clean=False
    try:
        evidence['before']=board.state();evidence['runtime_before']=board.runtime()
        t=evidence['before']['timetiles'];assert evidence['before']['firmware']=='7.9.20-timetiles'
        assert t['focus_status'] in (0,2) and t['countdown_status']==0 and not any(a[0] for a in t['alarms']), 'active TimeTiles session: preserve it'
        clean=True
        board.exchange(41,b'\x01\x0f',42)
        settled(board,lambda s:s['timetiles']['active'])
        command(board,8,struct.pack('<Q',int(time.time()*1000)))
        evidence['clock']=settled(board,lambda s:s['timetiles']['wall_valid'])
        assert abs(evidence['clock']['timetiles']['epoch_ms']-time.time()*1000)<1500
        for theme in range(6):
            command(board,7,bytes((theme,)))
            a=settled(board,lambda s:s['timetiles']['theme']==theme)
            for page in range(4):
                command(board,0,bytes((page,)))
                b=settled(board,lambda s:s['timetiles']['page']==page)
                assert b['timetiles']['renders']>=a['timetiles']['renders']
            evidence['themes'].append(b)
        if t['focus_status']==0:
            command(board,4,b'\x0f');command(board,0,b'\x01');command(board,1,b'\x01')
            a=settled(board,lambda s:s['timetiles']['focus_status']==1)
            time.sleep(1.1);b=board.state();assert b['timetiles']['focus_ms']<a['timetiles']['focus_ms']
            command(board,1,b'\x01');a=settled(board,lambda s:s['timetiles']['focus_status']==2)
            time.sleep(1.1);b=board.state();assert a['timetiles']['focus_ms']==b['timetiles']['focus_ms']
            evidence['focus_pause']=[a,b];command(board,1,b'\x01')
            evidence['focus_resume']=settled(board,lambda s:s['timetiles']['focus_status']==1)
            command(board,2,b'\x01');command(board,4,b'\x19')
        else:evidence['existing_paused_focus_preserved']=t['focus_ms']
        command(board,3,struct.pack('<I',3));command(board,0,b'\x02');command(board,1,b'\x02')
        evidence['countdown_start']=settled(board,lambda s:s['timetiles']['countdown_status']==1)
        board.exchange(41,b'\x01\x00',42);board.command(10,b'\x01')
        evidence['background_alert']=settled(board,lambda s:s['timetiles']['alerts'] and s['timetiles']['active'] and not s['asleep'])
        assert evidence['background_alert']['timetiles']['countdown_status']==3
        command(board,6,b'\x00');settled(board,lambda s:not s['timetiles']['alerts'])
        for i in range(6):command(board,5,bytes((i,8+i,10+i,0 if i%2==0 else 127,0)))
        evidence['six_alarms']=settled(board,lambda s:all(a[0] for a in s['timetiles']['alarms']))
        assert len(evidence['six_alarms']['timetiles']['alarms'])==6
        now=datetime.fromtimestamp(time.time(),timezone(timedelta(minutes=t['timezone'])))+timedelta(minutes=1)
        command(board,5,bytes((5,now.hour,now.minute,0,1)))
        evidence['alarm_scheduled']=settled(board,lambda s:s['timetiles']['alarms'][5][5]>0)
        target=evidence['alarm_scheduled']['timetiles']['alarms'][5][5]
        board.exchange(41,b'\x01\x00',42);board.command(10,b'\x01')
        print(json.dumps({'waiting_for_real_alarm_utc_ms':target}),flush=True)
        evidence['alarm_alert']=settled(board,lambda s:s['timetiles']['alerts'] and not s['asleep'],seconds=70)
        assert not evidence['alarm_alert']['timetiles']['alarms'][5][1]
        command(board,6,b'\x01')
        evidence['snooze']=settled(board,lambda s:s['timetiles']['alarms'][5][6]>0 and not s['timetiles']['alerts'])
        for i in range(6):command(board,9,bytes((i,)))
        for i in range(6):
            board.exchange(41,b'\x01\x00',42)
            settled(board,lambda s:not s['timetiles']['active'])
            time.sleep(.5);r=board.runtime();assert not any(x['name'].startswith('timetiles') for x in r['tasks'])
            evidence['reopen'].append(r)
            board.exchange(41,b'\x01\x0f',42);settled(board,lambda s:s['timetiles']['active'])
        command(board,3,struct.pack('<I',12));command(board,1,b'\x02')
        settled(board,lambda s:s['timetiles']['countdown_status']==1)
        time.sleep(1)
        board.exchange(41,b'\x01\x00',42)  # Close saves an actual remaining-time checkpoint.
        evidence['checkpoint']=settled(board,lambda s:not s['timetiles']['active'] and s['timetiles']['store_error']==0)
        board.wire.close()
        reboot=subprocess.run([sys.executable,'-m','esptool','--chip','esp32s3','--port',args.port,'--before','default_reset','--after','hard_reset','chip_id'],capture_output=True,text=True)
        evidence['reboot_output']=reboot.stdout+reboot.stderr;assert reboot.returncode==0
        board=connect(args.port)
        evidence['reboot']=settled(board,lambda s:s['timetiles']['countdown_status']==2)
        assert 0<evidence['reboot']['timetiles']['countdown_ms']<=evidence['checkpoint']['timetiles']['countdown_ms']+500
        if t['focus_status']==2:assert evidence['reboot']['timetiles']['focus_status']==2 and evidence['reboot']['timetiles']['focus_ms']==t['focus_ms']
        command(board,2,b'\x02');command(board,3,struct.pack('<I',600));command(board,7,bytes((t['theme'],)))
        command(board,8,struct.pack('<Q',int(time.time()*1000)))
        board.exchange(41,b'\x01\x00',42)
        a=settled(board,lambda s:s['wallpaper']['eaf']['direct'])
        time.sleep(1);b=board.state();evidence['home_resume']=[a,b]
        assert b['wallpaper']['eaf']['timing']['completed']>a['wallpaper']['eaf']['timing']['completed']
        assert b['wallpaper']['eaf']['timing']['errors']==0 and b['motion']['errors']==0
        for key in ('brightness','reduced_motion'):assert b[key]==evidence['before'][key]
        for key in ('selected','items','crcs','timing_crcs'):assert b['wallpaper'][key]==evidence['before']['wallpaper'][key]
        evidence['home_runtime']=board.runtime()
        board.exchange(41,b'\x01\x0f',42);command(board,0,b'\x00')
        evidence['after']=settled(board,lambda s:s['timetiles']['active'] and s['timetiles']['page']==0)
        evidence['runtime_after']=board.runtime();evidence['passed']=True
    except Exception as exc:
        evidence['error']=repr(exc)
        try:evidence['failure_state']=board.state();evidence['failure_runtime']=board.runtime()
        except Exception:pass
        if clean:
            try:
                for i in range(6):command(board,9,bytes((i,)))
                for _ in range(10):command(board,6,b'\x00')
                if t['focus_status']==0:command(board,2,b'\x01');command(board,4,b'\x19')
                command(board,2,b'\x02');command(board,3,struct.pack('<I',600));command(board,7,bytes((t['theme'],)))
                board.command(10,b'\x00');board.exchange(41,b'\x01\x0f',42);command(board,0,b'\x00')
            except Exception:pass
        raise
    finally:
        board.wire.close();args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(evidence,ensure_ascii=False,indent=2),encoding='utf-8')


if __name__=='__main__':main()
