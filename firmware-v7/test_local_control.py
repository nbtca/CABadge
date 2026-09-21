"""Compile the production local-input handler with a failing/successful storage substitute."""
from pathlib import Path
import os
import re
import subprocess

root=Path(__file__).resolve().parent
source=(root/'usb_screen/device/src/main.c').read_text(encoding='utf-8')
handler=re.search(r'static void local_control\(.*?\n}',source,re.S).group()
unit=root/'build/local_control_check.c'
unit.write_text(r'''#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
enum {ESP_OK=0,BADGE_APPLY=1,BADGE_BRIGHTNESS,BADGE_REDUCED,BADGE_SLEEP};
static struct {int wallpaper_index,brightness;bool reduced_motion;} state;
static int settings_result,storage_error,notices;
static bool applied,valid=true,asleep;
static bool badge_ui_select_wallpaper(int value){state.wallpaper_index=value;settings_result=storage_error;return valid;}
static void badge_ui_refresh(void){}
static void badge_ui_notice(const char *text){(void)text;notices++;}
static void badge_ui_apply_result(bool ok){applied=ok;}
static void badge_ui_sleep(bool value){asleep=value;}
static void badge_ui_settings(int value,bool reduced){state.brightness=value;state.reduced_motion=reduced;}
'''+handler+r'''
int main(void){
    local_control(BADGE_APPLY,1);assert(applied&&state.wallpaper_index==1);
    storage_error=7;local_control(BADGE_APPLY,2);assert(!applied&&state.wallpaper_index==1&&notices==1);
    storage_error=0;valid=false;local_control(BADGE_APPLY,2);assert(!applied&&state.wallpaper_index==1&&notices==2);
    local_control(BADGE_SLEEP,1);assert(asleep);
    local_control(BADGE_BRIGHTNESS,60);local_control(BADGE_REDUCED,1);assert(state.brightness==60&&state.reduced_motion);
    puts("Production local UI: storage success/failure/invalid selection and controls PASS");
}
''',encoding='utf-8')
env=os.environ.copy();env.update(TEMP='F:/CABadgeBuild/temp',TMP='F:/CABadgeBuild/temp',ZIG_GLOBAL_CACHE_DIR='F:/CABadgeBuild/zig-cache',ZIG_LOCAL_CACHE_DIR='F:/CABadgeBuild/zig-local-v7')
exe=root/'build/local_control_check.exe'
subprocess.run([str(root/'.venv/Lib/site-packages/ziglang/zig.exe'),'cc',str(unit),'-o',str(exe)],env=env,check=True)
subprocess.run([str(exe)],check=True)
