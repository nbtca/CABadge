"""Execute production TimeTiles core off-board; no UI or physical-board acceptance."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent
out = Path('F:/CABadgeBuild/temp/timetiles-core-test')
out.mkdir(parents=True, exist_ok=True)
code = r'''
#include "core.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#define JAN1 INT64_C(1704067200000) /* Monday 2024-01-01 UTC */
#define DAY INT64_C(86400000)
static void init(tt_core_t *c) { tt_init(c); c->timezone_minutes=0; tt_tick(c,1000,JAN1); }
static void timers(void) {
 tt_core_t c; init(&c);
 assert(c.date_key==20240101 && c.wall_valid && c.focus.duration_ms==1500000 && c.countdown.duration_ms==600000);
 tt_core_t before=c;
 assert(!tt_select(&c,4,true) && !tt_focus_duration(&c,0) && !tt_focus_duration(&c,30));
 assert(!tt_countdown_duration(&c,0) && !tt_countdown_duration(&c,86400));
 assert(!memcmp(&c,&before,sizeof(c)));
 assert(tt_countdown_duration(&c,1));
 tt_select(&c,TT_COUNTDOWN,false); assert(c.countdown.status_raw==0);
 tt_toggle(&c,false); tt_tick(&c,1400,JAN1+400); assert(tt_remaining(&c,false)==600);
 tt_toggle(&c,false); assert(c.countdown.status_raw==2);
 tt_tick(&c,9000,JAN1+8000); assert(tt_remaining(&c,false)==600);
 tt_toggle(&c,false); assert(c.countdown.status_raw==1);
 tt_select(&c,TT_CLOCK,false); tt_tick(&c,9599,JAN1+8599); assert(!c.alert_count);
 tt_tick(&c,9600,JAN1+8600); assert(c.countdown.status_raw==3 && c.alert_count==1 && c.alerts[0].type==2);
 tt_tick(&c,9700,JAN1+8700); assert(c.alert_count==1);
 tt_ack(&c,true); assert(c.page==TT_COUNTDOWN && c.countdown.status_raw==1 && tt_remaining(&c,false)==1000);
 tt_reset(&c,false); assert(c.countdown.status_raw==0 && tt_remaining(&c,false)==1000);
 uint32_t rev=c.revision; tt_reset(&c,false); assert(c.revision==rev);
 assert(tt_countdown_duration(&c,86399) && c.countdown.duration_ms==86399000);
 tt_tick(&c,UINT64_C(0xffffff00),JAN1+9000); tt_toggle(&c,false);
 uint64_t deadline=((uint64_t)c.countdown.deadline_high<<32)|c.countdown.deadline_low;
 assert(deadline==UINT64_C(0xffffff00)+86399000 && c.countdown.deadline_high>0);
 tt_tick(&c,UINT64_C(0xffffff10),JAN1+9016); assert(tt_remaining(&c,false)==86398984);
 tt_tick(&c,200,JAN1); assert(c.monotonic_ms==UINT64_C(0xffffff10));
 struct recovered_timer_block t={.status_raw=1,.deadline_high=3,.deadline_low=0};
 assert(recovered_timer_remaining_ms(&t,0)==UINT32_MAX);
 uint32_t revision=UINT32_MAX; t=(struct recovered_timer_block){.duration_ms=100,.status_raw=3};
 recovered_timer_prepare_start(&t,UINT64_MAX-10,&revision);
 assert(t.deadline_low==89 && !t.deadline_high && revision==0);
 puts("PASS start/pause/resume/reset/background expiry, repeat countdown, bounds, monotonic clamp and 64-bit helper math");
}
static void focus_cycles(void) {
 tt_core_t c; init(&c); assert(tt_focus_duration(&c,15));
 for(unsigned cycle=1;cycle<=4;cycle++) {
  tt_toggle(&c,true); assert(!c.focus_rest && c.focus.status_raw==1);
  uint64_t now=c.monotonic_ms+c.focus.duration_ms;
  tt_tick(&c,now,JAN1+(int64_t)now-1000);
  assert(c.focus.status_raw==3 && c.cycles==cycle && c.today==cycle && c.alerts[0].type==0);
  tt_ack(&c,true); assert(c.focus_rest && c.focus.status_raw==1);
  assert(c.focus.duration_ms==(cycle==4?900000u:300000u));
  if(cycle==4) { tt_reset(&c,true); assert(c.focus_rest && c.focus.duration_ms==900000); break; }
  now=c.monotonic_ms+c.focus.duration_ms;
  tt_tick(&c,now,JAN1+(int64_t)now-1000); assert(c.alerts[0].type==1 && c.today==cycle);
  tt_ack(&c,false); tt_toggle(&c,true); assert(!c.focus_rest && c.focus.status_raw==1);
  tt_reset(&c,true);
 }
 assert(tt_focus_duration(&c,60) && !c.focus_rest && c.focus.duration_ms==3600000);
 tt_toggle(&c,true); tt_tick(&c,c.monotonic_ms+3600000,JAN1+3*3600000);
 assert(c.focus.status_raw==3); tt_ack(&c,false);
 tt_select(&c,TT_FOCUS,true); assert(!c.focus_rest && tt_remaining(&c,true)==3600000);
 uint64_t saved=c.monotonic_ms; tt_tick(&c,saved,JAN1+DAY); assert(c.today==0 && c.date_key==20240102);
 c.today=2; c.timezone_minutes=-720; tt_tick(&c,saved,JAN1+DAY); assert(c.today==0 && c.date_key==20240101);
 c.timezone_minutes=0; tt_tick(&c,saved,JAN1+59*DAY); assert(c.date_key==20240229);
 tt_tick(&c,saved,JAN1+60*DAY); assert(c.date_key==20240301);
 tt_tick(&c,saved,INT64_C(1735689600000)); assert(c.date_key==20250101);
 tt_tick(&c,saved,INT64_MAX); assert(!c.wall_valid);
 tt_tick(&c,saved,-1); assert(!c.wall_valid);
 puts("PASS four focus cycles, short/long rests, manual next versus orientation restart, local-date reset and invalid wall time");
}
static void alarms(void) {
 tt_core_t c; init(&c);
 tt_core_t before=c;
 assert(!tt_alarm_set(&c,6,0,0,0,true) && !tt_alarm_set(&c,0,24,0,0,true));
 assert(!tt_alarm_set(&c,0,0,60,0,true) && !tt_alarm_set(&c,0,0,0,128,true));
 assert(!memcmp(&c,&before,sizeof(c)));
 for(unsigned i=0;i<6;i++)assert(tt_alarm_set(&c,i,0,1,0,true));
 tt_tick(&c,61000,JAN1+60000); assert(c.alert_count==6);
 for(unsigned i=0;i<6;i++) { assert(c.alerts[i].type==3 && c.alerts[i].alarm_index==i && !c.alarms[i].enabled); }
 tt_ack(&c,true); assert(c.alarms[0].snooze_deadline_ms==361000 && c.alerts[0].alarm_index==1);
 while(c.alert_count)tt_ack(&c,false);
 tt_tick(&c,360999,0); assert(!c.alert_count);
 tt_tick(&c,361000,0); assert(c.alert_count==1 && c.alerts[0].alarm_index==0 && !c.wall_valid);
 tt_alarm_delete(&c,0); tt_ack(&c,true); assert(!c.alarms[0].present && !c.alarms[0].snooze_deadline_ms);
 init(&c); assert(tt_alarm_set(&c,0,0,1,127,true));
 tt_tick(&c,61000,JAN1+60000); assert(c.alert_count==1 && c.alarms[0].enabled && c.alarms[0].wall_deadline_ms==JAN1+DAY+60000);
 tt_ack(&c,false);
 tt_tick(&c,62000,JAN1-1); tt_tick(&c,63000,JAN1+60000); assert(!c.alert_count);
 tt_tick(&c,64000,JAN1+DAY+150001); assert(c.alert_count==1 && c.alerts[0].late && c.alarms[0].last_fired_ms==JAN1+DAY+60000);
 assert(c.alarms[0].wall_deadline_ms==JAN1+2*DAY+60000);
 tt_ack(&c,false); tt_tick(&c,65000,JAN1+5*DAY+60000); assert(c.alert_count==1); /* one notice, not one per missed day */
 init(&c); assert(tt_alarm_set(&c,0,0,1,1,true)); assert(c.alarms[0].wall_deadline_ms==JAN1+6*DAY+60000); /* Sunday */
 assert(tt_alarm_set(&c,0,0,1,0x3e,true)); assert(c.alarms[0].wall_deadline_ms==JAN1+60000); /* weekdays */
 c.timezone_minutes=480; assert(tt_alarm_set(&c,0,8,1,127,true)); assert(c.alarms[0].wall_deadline_ms==JAN1+60000);
 tt_init(&c); c.timezone_minutes=0; assert(tt_alarm_set(&c,0,0,1,0,true)); assert(!c.alarms[0].wall_deadline_ms);
 tt_tick(&c,1000,JAN1+120000); assert(!c.alert_count && c.alarms[0].wall_deadline_ms==JAN1+DAY+60000); /* first sync: future only */
 puts("PASS six alarms, bounds, once/repeat/Sunday/weekdays, timezone, snooze without wall clock, deletion, clock jumps and first-sync scheduling");
}
static void queue(void) {
 tt_core_t c; init(&c); assert(tt_countdown_duration(&c,1));
 for(int i=0;i<12;i++) { tt_toggle(&c,false); tt_tick(&c,c.monotonic_ms+1000,JAN1); }
 assert(c.alert_count==10 && c.alert_dropped==2);
 for(int i=0;i<10;i++)tt_ack(&c,false);
 assert(!c.alert_count); tt_ack(&c,false); assert(!c.alert_count);
 puts("PASS 10-alert queue, drop-new overflow and safe oldest-first acknowledgements");
}
static void orientation(void) {
 tt_core_t c; init(&c); int16_t flat[]={0,0,1000},diag[]={800,800,0};
 assert(!tt_motion(&c,flat,1000) && !tt_motion(&c,diag,2000));
 const int16_t dirs[][3]={{0,1000,0},{1000,0,0},{0,-1000,0},{-1000,0,0}};
 for(unsigned i=0;i<4;i++) {
  uint64_t t=3000+i*1000;
  assert(!tt_motion(&c,dirs[i],t)); assert(!tt_motion(&c,dirs[i],t+319));
  assert(tt_motion(&c,dirs[i],t+320) && c.page==i);
  assert(!tt_motion(&c,dirs[i],t+800));
 }
 assert(c.focus.status_raw==1 && c.countdown.status_raw==1);
 int16_t boundary[]={649,0,0},depth[]={1000,0,761},ratio[]={1000,800,0};
 assert(!tt_motion(&c,boundary,8000) && !tt_motion(&c,depth,8100));
 assert(!tt_motion(&c,ratio,8200)); assert(tt_motion(&c,ratio,8520) && c.page==TT_FOCUS);
 tt_reset(&c,true); assert(!tt_motion(&c,dirs[1],9000)); /* no auto restart until changed posture */
 assert(!tt_motion(&c,dirs[2],10000)); assert(!tt_motion(&c,flat,10100));
 assert(!tt_motion(&c,dirs[2],10300)); assert(!tt_motion(&c,dirs[2],10619));
 assert(tt_motion(&c,dirs[2],10620));
 assert(!tt_motion(&c,dirs[0],11000)); assert(!tt_motion(&c,dirs[0],10900));
 assert(!tt_motion(&c,dirs[0],11219)); assert(tt_motion(&c,dirs[0],11220));
 puts("PASS four screen directions, 320ms stability, exact magnitude/depth/ratio bounds, flat reset and timestamp regression");
}
int main(void) { timers(); focus_cycles(); alarms(); queue(); orientation(); return 0; }
'''
(out/'check.c').write_text(code, encoding='utf-8')
env = dict(os.environ, TEMP='F:/CABadgeBuild/temp', TMP='F:/CABadgeBuild/temp',
           ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache')
sources = root/'ui/timetiles'
exe = out/'check.exe'
commands = [
    [str(root/'.venv/Lib/site-packages/ziglang/zig.exe'), 'cc', '-std=c11', '-O2',
     '-Wall', '-Wextra', '-Werror', '-UNDEBUG', '-I'+str(sources), str(out/'check.c'),
     str(sources/'core.c'), str(sources/'readable_timer_helpers.c'), '-o', str(exe)],
    [str(exe)],
]
log = root.parent/'outputs/timetiles-port-20261014/core-check.log'
log.parent.mkdir(parents=True, exist_ok=True)
with log.open('w', encoding='utf-8') as stream:
    for command in commands:
        result = subprocess.run(command, env=env, text=True, capture_output=True)
        output = result.stdout + result.stderr
        stream.write(output)
        stream.flush()
        print(output, end='')
        if result.returncode:
            raise SystemExit(result.returncode)
