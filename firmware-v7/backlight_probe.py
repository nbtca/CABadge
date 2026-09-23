"""Verify LEDC duty readback on the board; not an optical/current measurement."""
import json
import time
from pathlib import Path
from runtime_probe import Link

out=Path(__file__).resolve().parent.parent/'outputs/cabadge-v7-light-20260923'
link=Link('COM3')
rows=[]
try:
    link.request(32,b'\1',33)
    before=link.state()
    assert link.snap()['firmware']=='7.6.3-light'
    assert link.command(10,b'\0')==0
    for value in (10,25,50,75,100,20):
        assert link.command(9,bytes([value]))==0
        time.sleep(.5)
        snap=link.snap()
        expected=(value*value*1023+5000)//10000
        rows.append({'brightness':value,'expected':expected,'actual':snap['backlight_pwm']})
        assert snap['backlight_pwm']==expected,rows[-1]
        print(rows[-1],flush=True)
    assert link.command(10,b'\1')==0
    time.sleep(.3)
    off=link.snap()['backlight_pwm'];assert off==0
    assert link.command(10,b'\0')==0
    time.sleep(.3)
    on=link.snap()['backlight_pwm'];assert on==41
    after=link.state()
    preserved=all(before['wallpaper'].get(k)==after['wallpaper'].get(k) for k in ('items','selected','crc'))
    assert preserved
    out.joinpath('hardware-verification.json').write_text(json.dumps({'duty_steps':rows,'sleep':off,'wake':on,'wallpapers_preserved':preserved,'final_brightness':20,'limits':'LEDC software readback only; no optical, LED-current, thermal or physical slider-drag measurement.'},indent=2),'utf-8')
    print('PASS: PWM levels, sleep, wake, wallpapers; final brightness 20%',flush=True)
finally:
    try:link.command(9,bytes([20]))
    finally:link.c.close()
