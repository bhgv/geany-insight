#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from build_external_slang import apply_patch
from install_slang import stage

PATCH = b'diff --git a/file.txt b/file.txt\n--- a/file.txt\n+++ b/file.txt\n@@ -1,2 +1,2 @@\n-old\n+compatible\n context\n'

class SlangBuild(unittest.TestCase):
    def test_lf_crlf_and_idempotent_application_preserve_user_files(self):
        for source_eol in (b'\n', b'\r\n'):
            for patch_eol in (b'\n', b'\r\n'):
                with tempfile.TemporaryDirectory() as directory:
                    source = Path(directory) / 'source'; source.mkdir()
                    subprocess.check_call(['git','init','-q',str(source)])
                    original = source / 'file.txt'
                    original.write_bytes(b'old\ncontext\n'.replace(b'\n',source_eol))
                    user = source / 'my.cpp'; user.write_bytes(b'keep my edits\r\n')
                    patch = Path(directory) / 'compat.patch'; patch.write_bytes(PATCH.replace(b'\n',patch_eol))
                    apply_patch(source,patch); apply_patch(source,patch)
                    self.assertEqual(original.read_bytes(), b'compatible\ncontext\n'.replace(b'\n',source_eol))
                    self.assertEqual(user.read_bytes(),b'keep my edits\r\n')

    def test_conflict_does_not_overwrite_user_text(self):
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory); subprocess.check_call(['git','init','-q',directory])
            original=source/'file.txt'; original.write_bytes(b'my implementation\r\ncontext\r\n')
            patch=source/'compat.patch'; patch.write_bytes(PATCH)
            with self.assertRaisesRegex(ValueError,'conflicts'):
                apply_patch(source,patch)
            self.assertEqual(original.read_bytes(),b'my implementation\r\ncontext\r\n')

    def test_wrong_build_revision_never_creates_installation(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); build=root/'build'; build.mkdir()
            (build/'source-manifest.json').write_text(json.dumps({'revision':'wrong'}))
            with self.assertRaisesRegex(ValueError,'revision'):
                stage(build,root/'destination')
            self.assertFalse((root/'destination').exists())

if __name__ == '__main__':
    unittest.main()
