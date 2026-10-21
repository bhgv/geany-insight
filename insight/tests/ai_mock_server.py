#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import json
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def do_POST(self):
        data=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        if data['model']=='slow':time.sleep(5)
        answer={'explanation':'Mock analysis: source.c line 1 is missing a semicolon; compare the attached code with the compiler message. Review the proposed value and terminator before applying.', 'command':"printf 'review only'",'replacement':'int value = 2;\n'}
        raw=json.dumps({'choices':[{'message':{'content':json.dumps(answer)},'finish_reason':'stop'}]}).encode()
        self.send_response(200);self.end_headers()
        try:self.wfile.write(raw)
        except (BrokenPipeError,ConnectionResetError):pass
server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
print(server.server_port,flush=True)
server.serve_forever()
