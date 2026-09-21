"""Read task/heap counters and compare controlled everyday UI workloads over local USB."""
import argparse,json,struct,time,sys
from pathlib import Path
from datetime import datetime
import serial
from performance_probe import packet,Decoder

class Link:
    def __init__(self,port):
        self.c=serial.Serial(port=None,timeout=.08,write_timeout=2)
        self.c.dtr=self.c.rts=False;self.c.port=port;self.c.open();self.d=Decoder();self.serial=1000
    def request(self,kind,data=b'',reply=42):
        self.c.write(packet(kind,data));end=time.monotonic()+5
        while time.monotonic()<end:
            for k,b in self.d.feed(self.c.read(8192)):
                if k==reply:return b
        raise TimeoutError(f'packet {kind} -> {reply}')
    def snap(self,data=b''):return json.loads(self.request(41,data))
    def command(self,op,arg=b''):
        self.serial+=1;b=self.request(35,struct.pack('<I',self.serial)+bytes([1,op])+arg,36)
        if len(b)!=5 or struct.unpack('<I',b[:4])[0]!=self.serial:raise ValueError('ACK identity')
        return b[4]
    def state(self):return json.loads(self.request(34,reply=34))

def summarize(rows):
    if len(rows)<2:raise ValueError('too few snapshots')
    a,b=rows[0],rows[-1];elapsed=b['us']-a['us'];assert elapsed>0
    tasks=[];first={t['id']:t for t in a['tasks']}
    for t in b['tasks']:
        old=first.get(t['id'])
        if not old:continue
        delta=t['cpu_us']-old['cpu_us'];assert delta>=0
        tasks.append({'name':t['name'],'core':t['core'],'cpu_one_core_pct':round(100*delta/elapsed,2),'stack_free_min_bytes':min(x['stack_free_min'] for r in rows for x in r['tasks'] if x['id']==t['id'])})
    tasks.sort(key=lambda t:t['cpu_one_core_pct'],reverse=True)
    return {'seconds':round(elapsed/1e6,2),'render_fps':round((b['rendered']-a['rendered'])*1e6/elapsed,2),'lcd_submit_fps':round((b['submitted']-a['submitted'])*1e6/elapsed,2),
            'internal_free_min':min(r['heap'][0]['free'] for r in rows),'internal_largest_min':min(r['heap'][0]['largest'] for r in rows),'psram_free_min':min(r['heap'][1]['free'] for r in rows),
            'internal_free_end':b['heap'][0]['free'],'psram_free_end':b['heap'][1]['free'],'query_us_max':max(r['query_us'] for r in rows),
            'work_wall_pct':{k:round((y-x)*100/elapsed,3) for k,x,y in zip(('usb','wifi_poll','ble_poll','motion','wallpaper_poll','battery','management','gui_including_lcd'),a['work_us'],b['work_us'])},'tasks':tasks}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('port',nargs='?',default='COM3');parser.add_argument('--seconds',type=int,default=15);parser.add_argument('--self-test',action='store_true');parser.add_argument('--animations',action='store_true');parser.add_argument('--all',action='store_true');args=parser.parse_args()
    if args.self_test:
        a={'us':0,'rendered':0,'submitted':0,'heap':[{'free':100,'largest':80},{'free':200}],'query_us':10,'work_us':[0]*8,'tasks':[{'id':1,'name':'main','core':0,'cpu_us':0,'stack_free_min':900}]}
        b={**a,'us':2000000,'rendered':20,'submitted':18,'tasks':[{**a['tasks'][0],'cpu_us':1000000,'stack_free_min':800}]}
        r=summarize([a,b]);assert r['render_fps']==10 and r['lcd_submit_fps']==9 and r['tasks'][0]['cpu_one_core_pct']==50 and r['tasks'][0]['stack_free_min_bytes']==800
        print('Runtime deltas, distinct LCD/render counts and stack minimum PASS');return
    out=Path(__file__).resolve().parent.parent/'outputs/performance';out.mkdir(exist_ok=True)
    stem=out/('runtime-'+datetime.now().strftime('%Y%m%d-%H%M%S'));link=Link(args.port);first=original=None;result={'scenarios':[],'metrics':'CPU percent of one core; both cores total 200%. Stack high-water remaining bytes; heap shared, not per-task ownership. work_wall_pct includes preemption; GUI includes LCD wait.'}
    try:
        link.request(32,b'\x01',33);original=link.state();first=link.snap();assert first['firmware'] in ('7.2.0-monitor','7.2.1-monitor', '7.2.2-monitor', '7.2.3-monitor','7.3.0-fluid','7.3.1-internal','7.3.2-fluid','7.3.3-fluid','7.3.4-async','7.3.5-refresh16','7.3.6-anim16','7.3.7-mem')
        result['firmware']=first['firmware'];result['original']={k:original[k] for k in ('asleep','brightness','reduced_motion')};result['initial_snapshot']=first
        link.command(10,b'\x00');link.snap(b'\x02\x00')
        if args.animations:assert link.command(11,b'\x00')==0
        cases=[('wallpaper_idle',0,None,False),('menu_idle',3,None,False),('member_idle',7,None,False),('carousel',None,0,False),('drawers',None,1,False),('member_slide',None,2,False),('carousel_usb_status_5hz',None,0,True),('carousel_wifi_scan',None,0,False),('carousel_ble_scan',None,0,False),('wallpaper_hud_on',0,None,False),('carousel_hud_on',None,0,False),('sleep',0,None,False),('recovered_idle',0,None,False)]
        if args.animations and not args.all:cases=[c for c in cases if c[0] in ('wallpaper_idle','carousel','drawers','member_slide','carousel_usb_status_5hz','wallpaper_hud_on','carousel_hud_on','recovered_idle')]
        with stem.with_suffix('.jsonl').open('w',encoding='utf-8') as raw:
            for name,page,scene,state_load in cases:
                link.command(10,bytes([name=='sleep']))
                link.snap(bytes([1,page]) if page is not None else bytes([3,scene]))
                link.snap(bytes([2,int('hud_on' in name)]))
                rc=None
                if name.endswith('wifi_scan'):rc=link.command(2)
                if name.endswith('ble_scan'):rc=link.command(6)
                time.sleep(.5);rows=[];end=time.monotonic()+args.seconds;at=0;state_at=0
                while time.monotonic()<end:
                    now=time.monotonic()
                    if now>=at:
                        row=link.snap();rows.append(row);raw.write(json.dumps({'scenario':name,'snapshot':row})+'\n');raw.flush();at=now+1
                    if state_load and now>=state_at:link.state();state_at=now+.2
                    time.sleep(.02)
                status=link.state();summary=summarize(rows);summary.update(name=name,reduced_motion=status['reduced_motion'],scan_request_result=rc,wifi={k:status['wifi'][k] for k in ('enabled','connected','scanning','error')},ble={k:status['ble'][k] for k in ('enabled','advertising','scanning','error')})
                result['scenarios'].append(summary);stem.with_suffix('.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
                print(f"{name}: FPS={summary['lcd_submit_fps']} internal_free_min={summary['internal_free_min']} main_cpu={[t['cpu_one_core_pct'] for t in summary['tasks'] if t['name']=='main']}",flush=True)
    finally:
        try:
            if first is not None and original is not None:
                if args.animations:link.command(11,bytes([original['reduced_motion']]))
                link.snap(bytes([1,first['page']]));link.snap(bytes([2,int(first['hud'])]));link.command(10,bytes([original['asleep']]))
        finally:link.c.close()
    print(stem.with_suffix('.json'),flush=True)

if __name__=='__main__':main()
