"""Small host check of the real diagnostic history/JSON boundary; no UI benchmark."""
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent
out = Path('F:/CABadgeBuild/temp/map-perf-test')
(out / 'freertos').mkdir(parents=True, exist_ok=True)
(out / 'esp_heap_caps.h').write_text('''#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define heap_caps_calloc(n,s,c) calloc(n,s)
''')
(out / 'freertos/FreeRTOS.h').write_text('''typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
''')
(out / 'check.c').write_text('''#include "map_perf.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 char json[8192],tiny[8];
 assert(map_perf_query(0,json,sizeof(json)));puts(json);
 map_perf_init();assert(map_perf_query(0,json,sizeof(json)));puts(json);
 for(int i=0;i<70;i++){
  map_perf_t r={.serial=5,.world=1,.x=-8,.hit=true,.start=1000,.end=2000,.requested_lanes=2,.actual_lanes=1,.fallback_reason="DMA_RESERVE",.dma_free=23000,.result=MAP_TILE_TIMEOUT,.error_category="TIMEOUT",.error_detail="Tile exceeded 30 seconds",.http_errno=110};
  map_perf_publish(&r);assert(r.seq==(unsigned)i+1);
 }
 assert(!map_perf_query(0,tiny,sizeof(tiny)));
 assert(map_perf_query(0,json,sizeof(json)));puts(json);
 assert(map_perf_query(6,json,sizeof(json)));puts(json);
 assert(map_perf_query(7,json,sizeof(json)));puts(json);
 assert(map_perf_query(69,json,sizeof(json)));puts(json);
 assert(map_perf_query(70,json,sizeof(json)));puts(json);
 assert(map_perf_query(100,json,sizeof(json)));puts(json);
 return 0;
}
''')
src = root / 'usb_screen/device/src'
exe = out / 'check.exe'
env = dict(os.environ, TEMP='F:/CABadgeBuild/temp', TMP='F:/CABadgeBuild/temp',
           ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache')
subprocess.run([str(root / '.venv/Lib/site-packages/ziglang/zig.exe'), 'cc', '-O2', '-UNDEBUG',
                '-I' + str(out), '-I' + str(src), str(out / 'check.c'),
                str(src / 'map_perf.c'), '-o', str(exe)], env=env, check=True)
rows = [json.loads(line) for line in subprocess.check_output([str(exe)], text=True).splitlines()]
assert not rows[0]['available'] and rows[1]['available'] and rows[1]['record'] is None
assert rows[2]['lost'] == 6 and rows[2]['record']['seq'] == 7
assert rows[3]['lost'] == 0 and rows[3]['record'] == rows[2]['record']
assert rows[4]['record']['seq'] == 8 and rows[5]['record']['seq'] == 70
assert rows[6]['record'] is None and rows[7]['latest'] == 70
assert rows[2]['record']['requested_lanes'] == 2 and rows[2]['record']['actual_lanes'] == 1
assert rows[2]['record']['fallback_reason'] == 'DMA_RESERVE' and rows[2]['record']['dma_free'] == 23000
assert rows[2]['record']['result'] == -12 and rows[2]['record']['error_category'] == 'TIMEOUT' and rows[2]['record']['http_errno'] == 110
print('PASS: unavailable/empty history, bounded overflow, non-destructive cursor, JSON, small output buffer')
