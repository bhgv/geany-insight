#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
condition = threading.Condition()
seen = set()
class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass
    def do_POST(self):
        data = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        model = data['model']
        if self.headers.get('Authorization') != 'Bearer secret-' + model:
            self.send_error(403)
            return
        with condition:
            seen.add(model)
            condition.notify_all()
            if not condition.wait_for(lambda: {'left', 'right'} <= seen, timeout=4):
                self.send_error(503, 'Requests were not concurrent')
                return
        answer = {'explanation': 'concurrent answer ' + model,
                  'command': 'echo ' + model,
                  'replacement': 'int value = ' + ('2' if model == 'left' else '3') + ';\n'}
        raw = json.dumps({'choices': [{'message': {'content': json.dumps(answer)}, 'finish_reason': 'stop'}]}).encode()
        self.send_response(200)
        self.end_headers()
        try:
            self.wfile.write(raw)
        except (BrokenPipeError, ConnectionResetError):
            pass
server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
print(server.server_port, flush=True)
server.serve_forever()
