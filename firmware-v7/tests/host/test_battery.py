"""Check the actual voltage warning policy without changing board readings."""
from host_support import ROOT, BUILD_ROOT, TEST_TEMP, ZIG
import os
from pathlib import Path
import subprocess

root = ROOT
out = (TEST_TEMP/'battery-test')
out.mkdir(parents=True, exist_ok=True)
(out / 'check.c').write_text(r'''
#include "battery_policy.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 badge_battery_t b={.level=-1};
 badge_battery_sample(&b,-1,0);assert(b.level==-1&&!b.pending);
 badge_battery_sample(&b,4200,0);assert(b.level==4);
 badge_battery_sample(&b,3400,2000);assert(!b.pending);
 badge_battery_sample(&b,3600,10000);assert(!b.pending); /* transient dip */
 for(unsigned t=12000;t<=22000;t+=2000)badge_battery_sample(&b,3400,t);
 assert(b.pending&&b.latched&&b.level==0);
 badge_battery_sample(&b,3510,24000);assert(b.pending); /* defer across sleep/transition */
 b.pending=false;
 badge_battery_sample(&b,3400,40000);badge_battery_sample(&b,3400,60000);
 assert(!b.pending&&b.latched); /* no repeated warning */
 badge_battery_sample(&b,3800,62000);badge_battery_sample(&b,3800,90000);assert(b.latched);
 badge_battery_sample(&b,3800,92000);assert(!b.latched);
 badge_battery_sample(&b,3400,94000);badge_battery_sample(&b,3400,104000);assert(b.pending);
 badge_battery_sample(&b,0,106000);assert(b.level==-1&&!b.pending&&b.latched);
 b=(badge_battery_t){.level=-1};
 badge_battery_sample(&b,3400,UINT32_MAX-4999);badge_battery_sample(&b,3400,5000);
 assert(b.pending); /* tick wrap */
 puts("PASS: icon bands, invalid ADC, sustained low, no repeat, recovery, deferred warning, tick wrap");
}
''')
env = dict(os.environ, TEMP=str(TEST_TEMP), TMP=str(TEST_TEMP),
           ZIG_GLOBAL_CACHE_DIR=str(BUILD_ROOT/'zig-cache'))
exe = out / 'check.exe'
subprocess.run([str(ZIG), 'cc', '-O2', '-UNDEBUG',
                '-I' + str(root / 'ui'), str(out / 'check.c'), '-o', str(exe)], env=env, check=True)
subprocess.run([str(exe)], check=True)
