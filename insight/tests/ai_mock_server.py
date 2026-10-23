#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import base64
import json
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def do_POST(self):
        data=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        if data['model']=='slow':time.sleep(5)
        answer={'explanation':'Mock analysis: source.c line 1 is missing a semicolon; compare the attached code with the compiler message. Review the proposed value and terminator before applying.', 'command':"printf 'review only'",'replacement':'int value = 2;\n'}
        answer['files']=[{'name':'diagnostic.txt','mime':'text/plain','data':base64.b64encode(b'fixture attachment\n').decode()}]
        if data['model']=='editor':
            content=json.loads(data['messages'][1]['content'])
            answer={'explanation': 'Editor mock: '+content['question']+'; manual='+str('manual correction' in content['attached_code'])+'; project='+str('helper_function' in content['console']), 'command': None, 'replacement': content['attached_code']+'// generated variant\n'}
        raw=json.dumps({'choices':[{'message':{'content':json.dumps(answer)},'finish_reason':'stop'}]}).encode()
        self.send_response(200);self.end_headers()
        try:self.wfile.write(raw)
        except (BrokenPipeError,ConnectionResetError):pass
server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
print(server.server_port,flush=True)
server.serve_forever()
