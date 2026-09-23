"""Real browser UI against a local HTTP substitute; never opens hardware."""
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import threading
import zlib
import unittest
from playwright.sync_api import sync_playwright
from PIL import Image
import io
import json


class WebUploadTest(unittest.TestCase):
    def test_mobile_crop_and_upload(self):
        html = (Path(__file__).parent/'device/src/wallpaper.html').read_bytes()
        requests = []
        hold_upload=threading.Event(); release_upload=threading.Event()
        state={'protocol':1,'product':'CABadge','firmware':'7.1.0-preview','remaining':280,'battery_mv':4032,'brightness':70,'asleep':False,'reduced_motion':False,
               'command_id':0,'command_op':0,'command_result':0,'wallpaper':{'phase':0,'error':0,'received':0,'generation':1},
               'wifi':{'enabled':True,'connected':True,'scanning':False,'connecting':False,'ssid':'<img src=x onerror=alert(1)>','rssi':-40,'ip':'192.168.1.5','error':0,
                       'networks':[{'ssid':'<script>bad</script>','rssi':-50,'security':1}]},
               'ble':{'enabled':True,'advertising':True,'scanning':False,'connecting':False,'incoming':'','outgoing':'','error':0,
                      'devices':[{'name':'<svg onload=alert(1)>','type':0,'address':'010203040506','rssi':-42,'connectable':True}]}}
        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args): pass
            def do_GET(self):
                if self.path=='/status':
                    self.send_response(200);self.end_headers();self.wfile.write(json.dumps(state).encode());return
                self.send_response(200); self.send_header('Content-Type','text/html; charset=utf-8');self.end_headers();self.wfile.write(html)
            def do_POST(self):
                body=self.rfile.read(int(self.headers.get('Content-Length',0)))
                ok=self.headers.get('X-JX-Key') is None
                if self.path in ('/upload','/reset'):ok=ok and self.headers.get('X-JX-CRC')==f'{zlib.crc32(body):08x}'
                requests.append((self.path,body,ok))
                if ok and self.path=='/control':
                    state['command_id']+=1;state['command_op']=body[1]
                    if body[1]==10:state['asleep']=bool(body[2])
                elif ok and self.path=='/cancel':
                    state['wallpaper']['phase']=6;release_upload.set()
                elif ok:
                    state['wallpaper']['phase']=3;state['wallpaper']['generation']+=1
                    if self.path=='/upload' and hold_upload.is_set():release_upload.wait(8)
                self.send_response((202 if self.path=='/control' else 200) if ok else 403);self.end_headers()
                try:self.wfile.write(json.dumps({'result':0,'id':state['command_id']}).encode())
                except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass
        server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        try:
            with sync_playwright() as pw:
                browser=pw.chromium.launch(channel='msedge',headless=True)
                page=browser.new_page(viewport={'width':390,'height':844},device_scale_factor=1,is_mobile=True,has_touch=True)
                page.add_init_script("Object.defineProperty(AbortSignal, 'timeout', {value: undefined, configurable: true});")
                page.set_default_timeout(5000)
                errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
                page.goto(f'http://127.0.0.1:{server.server_port}/')
                self.assertEqual(page.locator('#key, #authorize').count(),0)
                self.assertNotIn('key=',page.url)
                data=io.BytesIO();Image.new('RGB',(720,360),'red').save(data,format='PNG')
                page.locator('#file').set_input_files({'name':'test.png','mimeType':'image/png','buffer':data.getvalue()})
                page.locator('#send').wait_for(state='visible');page.wait_for_function("!document.getElementById('send').disabled")
                page.locator('#zoom').fill('2');page.locator('#zoom').dispatch_event('input')
                page.locator('#send').click();page.wait_for_function("document.getElementById('send').textContent==='正在应用…'")
                self.assertNotEqual(page.locator('#status').inner_text(),'已保存并应用')
                state['wallpaper']['phase']=4
                page.wait_for_function("document.getElementById('status').textContent==='已保存并应用'")
                self.assertEqual(page.locator('#send').inner_text(),'已应用')
                self.assertEqual(requests[-1],('/upload',b'\0\xf8'*(360*360),True))
                # Same page, no reload or reset between successful uploads.
                for colour,pixel in [('lime',b'\xe0\x07'),('blue',b'\x1f\x00')]:
                    page.wait_for_function("!document.getElementById('file').disabled")
                    data=io.BytesIO();Image.new('RGB',(360,360),colour).save(data,format='PNG')
                    page.locator('#file').set_input_files({'name':colour+'.png','mimeType':'image/png','buffer':data.getvalue()})
                    page.wait_for_function("document.getElementById('status').textContent===''")
                    page.locator('#send').click()
                    page.wait_for_function("document.getElementById('send').textContent==='正在应用…'")
                    state['wallpaper']['phase']=4
                    page.wait_for_function("document.getElementById('status').textContent==='已保存并应用'")
                    self.assertEqual(requests[-1],('/upload',pixel*(360*360),True))
                page.screenshot(path=str(Path(__file__).parents[1]/'build/wallpaper-mobile-v7.png'),full_page=True)
                page.wait_for_function("!document.getElementById('reset').disabled")
                count=len(requests);page.locator('#reset').click()
                page.locator('#confirmNo').click();self.assertEqual(len(requests),count)
                page.locator('#reset').click();page.wait_for_timeout(240);self.assertTrue(page.locator('#confirm').evaluate("e=>e.classList.contains('open')&&!e.inert&&e.clientHeight>=80"));page.screenshot(path=str(Path(__file__).parents[1]/'build/v7-inline-confirm.png'),full_page=True)
                page.locator('#confirmYes').click()
                page.wait_for_function("document.getElementById('reset').textContent==='正在应用…'")
                state['wallpaper']['phase']=4
                page.wait_for_function("document.getElementById('status').textContent==='已保存并应用'")
                self.assertEqual(requests[-1],('/reset',b'',True))
                page.evaluate("document.getElementById('deviceTab').click()")
                page.wait_for_timeout(60)
                middle=page.evaluate("getComputedStyle(document.querySelector('nav'),'::before').transform")
                self.assertNotEqual(middle,'none')
                page.evaluate("document.getElementById('wallTab').click()")
                page.wait_for_timeout(220)
                self.assertEqual(page.evaluate("getComputedStyle(document.querySelector('nav'),'::before').transform"),'matrix(1, 0, 0, 1, 0, 0)')
                page.locator('#deviceTab').click();page.wait_for_function("document.getElementById('battery').textContent==='4.032 V'")
                self.assertIn('<img',page.locator('#wifiState').inner_text())
                self.assertEqual(page.locator('#networks script, #devices svg, #wifiState img').count(),0)
                page.wait_for_function("!document.getElementById('sleep').disabled");page.locator('#sleep').check();page.wait_for_function("document.getElementById('operation').textContent.includes('状态如下')")
                self.assertEqual(requests[-1],('/control',b'\x01\x0a\x01',True))
                page.locator('#networks button').click();page.locator('#password').fill('password123');page.locator('#wifiForm button[type=submit]').click();page.wait_for_function('pendingId>0')
                page.wait_for_function("document.getElementById('operation').textContent.includes('状态如下')")
                self.assertEqual(requests[-1][1],b'\x01\x03\x14\x0b<script>bad</script>password123')
                self.assertEqual(page.locator('#password').input_value(),'')
                page.screenshot(path=str(Path(__file__).parents[1]/'build/management-mobile-v7.png'),full_page=True)
                self.assertFalse(errors,errors)
                self.assertLessEqual(page.evaluate('document.documentElement.scrollWidth'),390)
                page.locator('#wallTab').click()
                page.locator('#file').set_input_files(str(Path(__file__).parents[1]/'ui/wallpaper.png'))
                page.wait_for_function("document.getElementById('status').textContent===''")
                page.screenshot(path=str(Path(__file__).parents[1]/'build/wallpaper-mobile-v7.png'),full_page=True)
                page.wait_for_function("!document.getElementById('send').disabled")
                hold_upload.set();page.locator('#send').click();page.locator('#cancelUpload').click()
                page.locator('#confirmNo').click();self.assertFalse(any(r[0]=='/cancel' for r in requests))
                page.locator('#cancelUpload').click();page.locator('#confirmYes').click()
                page.wait_for_function("document.getElementById('status').textContent==='上传已取消'")
                self.assertTrue(any(r[0]=='/cancel' and r[2] for r in requests))
                state['reduced_motion']=True
                page.wait_for_function("document.documentElement.classList.contains('reduce-motion')")
                page.locator('#deviceTab').click()
                self.assertEqual(page.evaluate("getComputedStyle(document.querySelector('nav'),'::before').transitionDuration"),'0s')
                self.assertFalse(errors,errors)
                browser.close()
        finally:
            server.shutdown();server.server_close()


if __name__=='__main__':unittest.main()
