#!/usr/bin/env python3
"""Build the dependency-free diff core and verify exact block reconstruction."""
import ctypes
import pathlib
import os
import random
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
class Hunk(ctypes.Structure):
    _fields_ = [(name, ctypes.c_size_t) for name in ('a_begin', 'a_end', 'b_begin', 'b_end')]
class Diff(ctypes.Structure):
    _fields_ = [('hunks', ctypes.POINTER(Hunk)), ('count', ctypes.c_size_t)]

with tempfile.TemporaryDirectory() as tmp:
    libpath = pathlib.Path(tmp) / ('diff.dll' if os.name == 'nt' else 'diff.so')
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-shared', *([] if os.name == 'nt' else ['-fPIC']),
                    str(root / 'insight/core/diff.c'), '-o', str(libpath)], check=True)
    lib = ctypes.CDLL(str(libpath))
    lib.insight_diff_compute.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(Diff)]
    lib.insight_diff_map_offset.argtypes = [ctypes.POINTER(Diff), ctypes.c_int, ctypes.c_size_t]
    lib.insight_diff_map_offset.restype = ctypes.c_size_t
    lib.insight_diff_clear.argtypes = [ctypes.POINTER(Diff)]
    def check(a, b, expected=None):
        a, b = a.encode(), b.encode()
        diff = Diff()
        assert lib.insight_diff_compute(a, b, ctypes.byref(diff)) == 0
        try:
            if expected is not None:
                assert diff.count == expected
            for side, source, target in [(0, a, b), (1, b, a)]:
                previous = 0
                for offset in range(len(source) + 1):
                    mapped = lib.insight_diff_map_offset(ctypes.byref(diff), side, offset)
                    assert previous <= mapped <= len(target)
                    previous = mapped
                    # UTF-8 boundaries must remain valid for GTK character offsets.
                    if offset == len(source) or source[offset] & 0xc0 != 0x80:
                        target[:mapped].decode('utf-8')
                for h in diff.hunks[:diff.count]:
                    start, end = (h.a_begin, h.a_end) if side == 0 else (h.b_begin, h.b_end)
                    other_start = h.b_begin if side == 0 else h.a_begin
                    if start < end:
                        assert lib.insight_diff_map_offset(ctypes.byref(diff), side, start) == other_start
            pos_a = pos_b = 0
            reconstructed = bytearray()
            for h in diff.hunks[:diff.count]:
                assert pos_a <= h.a_begin <= h.a_end <= len(a)
                assert pos_b <= h.b_begin <= h.b_end <= len(b)
                assert a[pos_a:h.a_begin] == b[pos_b:h.b_begin]
                reconstructed.extend(a[pos_a:h.a_begin])
                reconstructed.extend(b[h.b_begin:h.b_end])
                pos_a, pos_b = h.a_end, h.b_end
            assert a[pos_a:] == b[pos_b:]
            reconstructed.extend(a[pos_a:])
            assert reconstructed == b
        finally:
            lib.insight_diff_clear(ctypes.byref(diff))
    for a, b, n in [('', '', 0), ('a\n', 'a\n', 0), ('', 'a\n', 1),
                    ('a\n', '', 1), ('a', 'a\n', 1), ('a\r\n', 'a\n', 1),
                    ('a\nb\nc\nd\n', 'a\nx\nc\ny\n', 2),
                    ('привет\nмир', 'привет\nземля\n', 1)]:
        check(a, b, n)
    rng = random.Random(413)
    for _ in range(3000):
        def text():
            return ''.join(rng.choice(['a\n', 'b\n', '\n', 'я\r\n']) for _ in range(rng.randrange(30))) + rng.choice(['', 'tail'])
        check(text(), text())
    huge = ('a\n' * 2100).encode()
    diff = Diff()
    assert lib.insight_diff_compute(huge, b'b\n' * 2100, ctypes.byref(diff)) == -2
    assert diff.count == 0 and not diff.hunks
    check('a\n' * 10000, 'a\n' * 9999 + 'b\n', 1)
    if os.name == 'nt':
        import _ctypes
        _ctypes.FreeLibrary(lib._handle)  # Windows locks loaded DLLs during cleanup.
print('Diff core: edge cases, 3000 randomized reconstructions, work limit and common-edge trimming passed.')
