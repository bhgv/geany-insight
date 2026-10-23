#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import base64
import io
import gzip
import tarfile
import zipfile
import importlib.util
import json
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
spec = importlib.util.spec_from_file_location('ai_client', Path(__file__).parents[1] / 'tools' / 'ai_client.py')
client = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client)
class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_POST(self):
        payload = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        self.server.last = (self.headers.get('Authorization'), payload)
        if self.path == '/redirect':
            self.send_response(302); self.send_header('Location', self.server.url + '/never'); self.end_headers(); return
        if self.path == '/error':
            self.send_response(401); self.end_headers(); self.wfile.write(b'private body must not be echoed'); return
        model = payload['model']
        if model == 'plain': content = 'Plain answer without a code edit.'
        else: content = json.dumps({'explanation': 'source.c:1 lacks a semicolon. α', 'command': 'printf safe' if model != 'control' else 'a\nb', 'replacement': 'int value = 2;\n', 'files': [{'name':'report.txt','mime':'text/plain','data':base64.b64encode(b'checked\n').decode()}]}, ensure_ascii=False)
        result = {'choices': [{'message': {'content': content}, 'finish_reason': 'length' if model == 'truncated' else 'stop'}]}
        raw = json.dumps(result).encode(); self.send_response(200); self.end_headers(); self.wfile.write(raw)
class Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler); cls.server.url = 'http://127.0.0.1:%d' % cls.server.server_port
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True); cls.thread.start()
    @classmethod
    def tearDownClass(cls): cls.server.shutdown(); cls.server.server_close(); cls.thread.join()
    def data(self, model='mock'):
        return dict(endpoint=self.server.url + '/chat/completions', model=model, key='fake-key', question='Explain this error', context='source.c:1 error', code='int value = 1\n', location='source.c', history='Earlier question')
    def test_compatible_protocol(self):
        for provider in ['OpenAI', 'Grok', 'DeepSeek', 'Qwen']:
            data=self.data(provider); answer=client.request(data); self.assertTrue(answer['has_replacement']); self.assertEqual(answer['replacement'], 'int value = 2;\n')
            auth, body=self.server.last; self.assertEqual(auth, 'Bearer fake-key'); self.assertFalse(body['stream']); self.assertEqual(body['model'], provider)
            context=json.loads(body['messages'][1]['content']); self.assertEqual(context['attached_code'],data['code']); self.assertEqual(context['console'],data['context']); self.assertEqual(context['earlier_conversation'],data['history'])
    def test_no_attachment_and_empty_attachment(self):
        data=self.data();data['code']='';data['location']='';self.assertFalse(client.request(data)['has_replacement']);data['location']='empty.c';self.assertTrue(client.request(data)['has_replacement'])
    def test_plain_and_command(self):
        self.assertFalse(client.request(self.data('plain'))['has_replacement']);self.assertEqual(client.request(self.data('control'))['command'],'')
    def test_errors_and_redirect(self):
        for path in ['/error','/redirect']:
            data=self.data();data['endpoint']=self.server.url+path
            with self.assertRaises(ValueError) as ctx: client.request(data)
            self.assertNotIn('private body',str(ctx.exception)); self.assertNotIn('fake-key',str(ctx.exception))
        with self.assertRaises(ValueError):client.request(self.data('truncated'))
    def test_file_parts_and_generated_bytes(self):
        data=self.data()
        archive=io.BytesIO()
        with zipfile.ZipFile(archive,'w') as bundle:bundle.writestr('source.c','int value;\n')
        values=[('image.png','image/png',b'PNG bytes'),('notes.txt','text/plain',b'notes'),('bundle.zip','application/zip',archive.getvalue())]
        data['attachments']='\n'.join('[attachment-%d]\nname=%s\nmime=%s\ndata=%s' % (i,n,m,base64.b64encode(raw).decode()) for i,(n,m,raw) in enumerate(values))
        answer=client.request(data)
        parts=self.server.last[1]['messages'][1]['content']
        self.assertEqual(parts[1]['image_url']['url'],'data:image/png;base64,'+base64.b64encode(values[0][2]).decode())
        self.assertIn('notes.txt',parts[2]['text'])
        self.assertIn('source.c',parts[3]['text']);self.assertIn('int value;',parts[3]['text'])
        self.assertEqual(base64.b64decode(answer['files'][0]['data']),b'checked\n')
        for value in [{'name':'../bad','mime':'text/plain','data':''},{'name':'a','mime':'text/plain','data':'!'}, {'name':'a','mime':'bad\nvalue','data':''}]:
            with self.assertRaises(ValueError):client.files_checked([value],262144)
        with self.assertRaises(ValueError):client.files_checked([{'name':'a','mime':'text/plain','data':base64.b64encode(b'x'*20).decode()}],10)
        bomb=io.BytesIO()
        with zipfile.ZipFile(bomb,'w',compression=zipfile.ZIP_DEFLATED) as bundle:bundle.writestr('large.txt','x'*524289)
        with self.assertRaises(ValueError):client.archive_preview({'name':'large.zip','data':base64.b64encode(bomb.getvalue()).decode()})
        with self.assertRaises(ValueError):client.input_files('[DEFAULT]\nname=injected\n[attachment]\nmime=text/plain\ndata=')
    def test_tar_gzip_previews_and_generated_file_limits(self):
        stream=io.BytesIO()
        with tarfile.open(fileobj=stream,mode='w:gz') as archive:
            info=tarfile.TarInfo('../source.c');body=b'int value;\n';info.size=len(body);archive.addfile(info,io.BytesIO(body))
            link=tarfile.TarInfo('outside');link.type=tarfile.SYMTYPE;link.linkname='/etc/passwd';archive.addfile(link)
        preview=client.archive_preview({'name':'sources.tar.gz','data':base64.b64encode(stream.getvalue()).decode()})
        self.assertIn('int value;',preview);self.assertIn('not followed',preview)
        preview=client.archive_preview({'name':'error.log.gz','data':base64.b64encode(gzip.compress(b'compiler error\n')).decode()})
        self.assertIn('compiler error',preview)
        with self.assertRaises(ValueError):client.archive_preview({'name':'bomb.tar.gz','data':base64.b64encode(gzip.compress(b'x'*1048577)).decode()})
        empty={'name':'empty.txt','mime':'text/plain','data':''}
        self.assertEqual(client.files_checked([empty],0),[empty])
        with self.assertRaises(ValueError):client.files_checked([empty]*5,262144)
    def test_validation(self):
        for url in ['http://example.org/a','file:///etc/passwd','https://u:p@example.org','https://example.org?a=key','https://example.org/#token']:
            with self.assertRaises(ValueError):client.endpoint_valid(url)
        for url in ['https://api.openai.com/v1/chat/completions','http://127.0.0.1:1/a','http://[::1]:1/a']:client.endpoint_valid(url)
        for key,value in [('model',''),('question',''),('key','key\nheader'),('context','x'*65537),('code','x'*131073)]:
            data=self.data();data[key]=value
            with self.assertRaises(ValueError):client.request(data)
if __name__=='__main__':unittest.main()
