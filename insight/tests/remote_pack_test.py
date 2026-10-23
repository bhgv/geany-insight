#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import gzip
import importlib.util
import os
from pathlib import Path
import tarfile
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('remote_pack', Path(__file__).parents[1] / 'tools/remote_pack.py')
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)

class Packing(unittest.TestCase):
    def test_binary_gzip_and_exclusive_output(self):
        with tempfile.TemporaryDirectory() as d:
            source, target = Path(d) / 'a.bin', Path(d) / 'a.gz'
            source.write_bytes(bytes(range(256)) * 10000)
            helper.pack('gz', source, target)
            self.assertEqual(gzip.decompress(target.read_bytes()), source.read_bytes())
            with self.assertRaises(FileExistsError):
                helper.pack('gz', source, target)
    def test_folder_names_and_symlink_not_followed(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            source = root / 'folder with spaces'
            source.mkdir()
            (source / 'source.c').write_text('int value;\n')
            if os.name != 'nt':
                (source / 'outside').symlink_to(root, target_is_directory=True)
            target = root / 'folder.tar.gz'
            helper.pack('tar.gz', source, target)
            with tarfile.open(target) as archive:
                self.assertEqual(archive.extractfile('folder with spaces/source.c').read(), b'int value;\n')
                if os.name != 'nt':
                    self.assertTrue(archive.getmember('folder with spaces/outside').issym())
                    self.assertEqual(len(archive.getmembers()), 3)
            with self.assertRaises(ValueError):
                helper.pack('gz', source, root / 'bad.gz')
    @unittest.skipUnless(os.name == 'posix', 'FIFO is a POSIX fixture')
    def test_fifo_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            source = Path(d) / 'source'
            source.mkdir()
            os.mkfifo(source / 'pipe')
            with self.assertRaises(ValueError):
                helper.pack('tar.gz', source, Path(d) / 'bad.tar.gz')

if __name__ == '__main__':
    unittest.main()
