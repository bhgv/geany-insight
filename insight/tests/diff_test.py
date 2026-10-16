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
    Cancel = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p)
    lib.insight_diff_compute_cancellable.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint, Cancel, ctypes.c_void_p, ctypes.POINTER(Diff)]
    lib.insight_diff_compute_options.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint, ctypes.POINTER(Diff)]
    lib.insight_diff_map_line.argtypes = [ctypes.POINTER(Diff), ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t]
    lib.insight_diff_map_line.restype = ctypes.c_size_t
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
                    ('привет\nмир', 'привет\nземля\n', 1),
                    ('a\rb\rc\rd\r', 'a\rx\rc\ry\r', 2),
                    ('a\rb\r\nc\nd', 'a\rx\r\nc\ny', 2),
                    ('a\r', 'a\r\n', 1), ('\r\r', '\r', 1)]:
        check(a, b, n)
    rng = random.Random(413)
    for _ in range(3000):
        def text():
            return ''.join(rng.choice(['a\n', 'b\n', '\n', 'я\r\n', 'c\r', '\r', 'd\n\r']) for _ in range(rng.randrange(30))) + rng.choice(['', 'tail'])
        check(text(), text())
    huge = ('a\n' * 2100).encode()
    diff = Diff()
    assert lib.insight_diff_compute(huge, b'b\n' * 2100, ctypes.byref(diff)) == -2
    assert diff.count == 0 and not diff.hunks
    check('a\n' * 10000, 'a\n' * 9999 + 'b\n', 1)
    # One-sided changes have a known exact answer; no LCS work is needed.
    check('', '\n' * 4000001, 1)
    check('\r' * 4000001, '', 1)
    check('head\ntail\n', 'head\n' + 'new\r\n' * 10000 + 'tail\n', 1)
    check('head\n' + 'old\r' * 10000 + 'tail\n', 'head\ntail\n', 1)
    def canonical(data, flags):
        import re
        if flags & 1:
            data = data.translate(None, b' \t\v\f')
        if flags & 2:
            data = re.sub(b'\r\n|\r|\n', b'\n', data)
        return data

    def options_check(a, b, flags, expected=None):
        a, b = a.encode(), b.encode()
        diff = Diff()
        assert lib.insight_diff_compute_options(a, b, flags, ctypes.byref(diff)) == 0
        try:
            if expected is not None:
                assert diff.count == expected, (a, b, flags, diff.count)
            pa = pb = 0
            merged = bytearray()
            for h in diff.hunks[:diff.count]:
                assert canonical(a[pa:h.a_begin], flags) == canonical(b[pb:h.b_begin], flags)
                merged.extend(a[pa:h.a_begin])
                merged.extend(b[h.b_begin:h.b_end])
                pa, pb = h.a_end, h.b_end
            assert canonical(a[pa:], flags) == canonical(b[pb:], flags)
            merged.extend(a[pa:])
            assert canonical(bytes(merged), flags) == canonical(b, flags), (a, b, flags, bytes(merged), [(h.a_begin,h.a_end,h.b_begin,h.b_end) for h in diff.hunks[:diff.count]])
            for side, source, target in [(0, a, b), (1, b, a)]:
                import re
                source_lines = len(re.findall(b'\r\n|\r|\n', source)) + bool(source and source[-1:] not in (b'\r', b'\n'))
                target_lines = len(re.findall(b'\r\n|\r|\n', target)) + bool(target and target[-1:] not in (b'\r', b'\n'))
                last = 0
                for line in range(source_lines + 1):
                    mapped = lib.insight_diff_map_line(ctypes.byref(diff), side, source, target, line)
                    assert last <= mapped <= target_lines
                    last = mapped
        finally:
            lib.insight_diff_clear(ctypes.byref(diff))

    for flags in range(4):
        options_check('a b\r\nя\r\n', 'ab\nя\n', flags, 0 if flags == 3 else 1)
        options_check('ab', 'a b', flags, 0 if flags & 1 else 1)
        options_check('a\r', 'a\n', flags, 0 if flags & 2 else 1)
        options_check('a', 'a\n', flags, 1)
        options_check('ab\n', 'a\nb\n', flags, 1)
        for _ in range(500):
            def option_text():
                return ''.join(rng.choice(['a b\n', 'ab\r\n', '\tя\r', ' x \n', '\n']) for _ in range(rng.randrange(20)))
            options_check(option_text(), option_text(), flags)
    for threshold in (1, 2, 3, 12, 800):
        calls = [0]
        @Cancel
        def cancelled(_):
            calls[0] += 1
            return calls[0] >= threshold
        diff = Diff()
        assert lib.insight_diff_compute_cancellable(b'a\n' * 1000, b'b\n' * 1000, 0, cancelled, None, ctypes.byref(diff)) == -3
        assert not diff.hunks and diff.count == 0
    if os.name == 'nt':
        import _ctypes
        _ctypes.FreeLibrary(lib._handle)  # Windows locks loaded DLLs during cleanup.
print('Diff options: 2000 normalized reconstructions, logical line mapping and exact original hunk bytes passed.')
print('Diff core: edge cases, 3000 randomized reconstructions, work limit and common-edge trimming passed.')
