#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Internal SDK discovery must be complete, pinned, and independent of PATH."""
import json
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'insight/tools'))
from select_clang_sdk import resolve
from build_external_clang import PIN


class InternalSdk(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.sdk = Path(self.temp.name) / 'internal sdk'
        for item in ('include/clang/Tooling/Tooling.h', 'include/llvm/Config/llvm-config.h',
                     'lib/libclang-cpp.so', 'lib/libLLVM.so', 'bin/llvm-config'):
            path = self.sdk / item
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('fixture', encoding='utf-8')
        (self.sdk / 'source-manifest.json').write_text(json.dumps({'revision': PIN, 'locally_modified': True}))

    def test_complete_sdk_and_local_source_edits(self):
        paths = resolve(self.sdk)
        self.assertEqual(paths[0], self.sdk.resolve())
        self.assertTrue(all(p.is_relative_to(self.sdk) for p in paths))
        self.assertEqual(paths[1].name, 'libclang-cpp.so')

    def test_platform_library_names(self):
        (self.sdk / 'lib/libclang-cpp.so').unlink()
        (self.sdk / 'lib/libLLVM.so').unlink()
        for cpp, llvm in (('libclang-cpp.dylib', 'libLLVM.dylib'),
                          ('libclang-cpp.dll.a', 'libLLVM.dll.a'),
                          ('clang-cpp.lib', 'LLVM.lib')):
            with self.subTest(cpp=cpp):
                for name in (cpp, llvm):
                    (self.sdk / 'lib' / name).write_text('fixture')
                paths = resolve(self.sdk)
                self.assertEqual(paths[1].name, cpp)
                self.assertEqual(paths[2].name, llvm)
                for name in (cpp, llvm):
                    (self.sdk / 'lib' / name).unlink()

    def test_missing_library_does_not_use_path(self):
        (self.sdk / 'lib/libclang-cpp.so').unlink()
        with self.assertRaisesRegex(ValueError, 'libclang-cpp'):
            resolve(self.sdk)
        environment = dict(os.environ, PATH=os.defpath)
        result = subprocess.run([sys.executable, str(ROOT / 'insight/tools/select_clang_sdk.py'),
                                 '--sdk', str(self.sdk)], env=environment, capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, '')
        self.assertIn('clang-provider=system', result.stderr)

    def test_missing_generated_header(self):
        (self.sdk / 'include/llvm/Config/llvm-config.h').unlink()
        with self.assertRaisesRegex(ValueError, 'llvm-config.h'):
            resolve(self.sdk)

    def test_wrong_revision(self):
        (self.sdk / 'source-manifest.json').write_text('{"revision":"other"}')
        with self.assertRaisesRegex(ValueError, 'revision'):
            resolve(self.sdk)

    def test_invalid_manifest_shape(self):
        (self.sdk / 'source-manifest.json').write_text('[]')
        with self.assertRaisesRegex(ValueError, 'manifest'):
            resolve(self.sdk)

    def test_missing_manifest(self):
        (self.sdk / 'source-manifest.json').unlink()
        with self.assertRaisesRegex(ValueError, 'manifest'):
            resolve(self.sdk)

    def test_staged_library_symlinks(self):
        if os.name == 'nt':
            self.skipTest('Creating developer symlinks may require Windows privileges')
        shared = self.sdk.parent / 'build-libLLVM.so'
        shared.write_text('built from sources')
        target = self.sdk / 'lib/libLLVM.so'
        target.unlink()
        target.symlink_to(shared)
        self.assertEqual(resolve(self.sdk)[2], target)


if __name__ == '__main__':
    unittest.main()
