"""Real S3 board: five FluidBox modes, sleep/reset/close/reopen and settings preservation."""
import argparse
import json
from pathlib import Path
import statistics
import time
from heart_rate_probe import Board


def settled(board,predicate):
    deadline=time.monotonic()+8
    while True:
        state=board.state()
        if predicate(state):return state
        if time.monotonic()>=deadline:raise AssertionError('board state did not settle: '+str(state.get('fluidbox')))
        time.sleep(.2)


def collect(board,mode,seconds):
    board.exchange(41,bytes((12,mode)),42)
    settled(board,lambda s:s['fluidbox']['active'] and s['fluidbox']['mode']==mode)
    time.sleep(1)
    samples=[]
    deadline=time.monotonic()+seconds
    while True:
        start=time.monotonic();state=board.state();runtime=board.runtime()
        samples.append({'host_seconds':(start+time.monotonic())/2,'state':state,'runtime':runtime})
        assert state['fluidbox']['active'] and state['fluidbox']['running'] and state['fluidbox']['particles']==1200
        assert state['motion']['ready'] and state['motion']['errors']==0
        if time.monotonic()>=deadline:break
        time.sleep(.5)
    a,b=samples[0],samples[-1];dt=b['host_seconds']-a['host_seconds']
    summary={'mode':mode,'host_seconds':dt,
        'gui_image_fps':(b['state']['fluidbox']['frames']-a['state']['fluidbox']['frames'])/dt,
        'lcd_completed_fps':(b['runtime']['submitted']-a['runtime']['submitted'])/dt,
        'sim_hz':(b['state']['fluidbox']['steps']-a['state']['fluidbox']['steps'])/dt,
        'render_us_median':statistics.median(s['state']['fluidbox']['render_us'] for s in samples),
        'sim_us_median':statistics.median(s['state']['fluidbox']['sim_us'] for s in samples),
        'internal_free_min':min(s['runtime']['heap'][0]['free'] for s in samples),
        'psram_free_min':min(s['runtime']['heap'][1]['free'] for s in samples)}
    assert summary['gui_image_fps']>0 and summary['lcd_completed_fps']>0 and summary['sim_hz']>0
    print(json.dumps(summary),flush=True)
    return {'summary':summary,'samples':samples}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',default='COM3')
    parser.add_argument('--seconds',type=int,default=10)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();board=Board(args.port);evidence={'modes':[],'reopen':[]}
    try:
        evidence['before']=board.state();assert evidence['before']['firmware']=='7.9.19-fluidbox'
        board.exchange(41,b'\x01\x0e',42)
        settled(board,lambda s:s['fluidbox']['active'] and s['fluidbox']['frames']>2)
        for mode in range(5):evidence['modes'].append(collect(board,mode,args.seconds))
        board.exchange(41,b'\x0c\x05',42)
        evidence['reset']=settled(board,lambda s:s['fluidbox']['active'])
        board.command(10,b'\x01')
        a=settled(board,lambda s:s['asleep'] and not s['fluidbox']['running'])
        time.sleep(1.2);b=board.state();evidence['sleep']=[a,b]
        assert a['fluidbox']['steps']==b['fluidbox']['steps'] and a['fluidbox']['frames']==b['fluidbox']['frames']
        board.command(10,b'\x00')
        evidence['wake']=settled(board,lambda s:not s['asleep'] and s['fluidbox']['steps']>b['fluidbox']['steps'])
        for i in range(6):
            board.exchange(41,b'\x01\x01',42)
            settled(board,lambda s:not s['fluidbox']['active'])
            time.sleep(.5);closed=board.runtime()
            assert not any(t['name'].startswith('fluid_') for t in closed['tasks'])
            evidence['reopen'].append(closed)
            board.exchange(41,b'\x01\x0e',42)
            settled(board,lambda s:s['fluidbox']['active'] and s['fluidbox']['frames']>2)
        heap=[r['heap'][0]['free'] for r in evidence['reopen']]
        assert max(heap)-min(heap)<4096,'internal heap falls across reopening'
        # Existing raster caches may retain/evict one 259200-byte page during warmup.
        psram=[r['heap'][1]['free']+r['transition_cache']['bytes'] for r in evidence['reopen']]
        assert max(psram)-min(psram)<4096,'PSRAM falls across reopening'
        board.exchange(41,b'\x01\x00',42)
        a=settled(board,lambda s:not s['fluidbox']['active'] and s['wallpaper']['eaf']['direct'])
        time.sleep(1);b=board.state();evidence['home_resume']=[a,b]
        assert b['wallpaper']['eaf']['timing']['completed']>a['wallpaper']['eaf']['timing']['completed']
        assert b['wallpaper']['eaf']['timing']['errors']==0
        for key in ('brightness','reduced_motion'):assert b[key]==evidence['before'][key]
        for key in ('selected','items','crcs','timing_crcs'):assert b['wallpaper'][key]==evidence['before']['wallpaper'][key]
        evidence['home_runtime']=board.runtime()
        board.exchange(41,b'\x01\x0e',42)
        settled(board,lambda s:s['fluidbox']['active'])
        board.exchange(41,b'\x0c\x02',42)
        evidence['after']=settled(board,lambda s:s['fluidbox']['mode']==2 and s['fluidbox']['frames']>2)
        evidence['runtime_after']=board.runtime();evidence['passed']=True
    except Exception as exc:
        evidence['error']=repr(exc)
        try:evidence['failure_state']=board.state();evidence['failure_runtime']=board.runtime()
        except Exception:pass
        raise
    finally:
        board.wire.close();args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(evidence,ensure_ascii=False,indent=2),encoding='utf-8')


if __name__=='__main__':main()
