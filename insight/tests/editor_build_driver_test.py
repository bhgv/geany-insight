#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import build_editor
import prepare_clang_sdk
from build_external_clang import PIN


def sdk_fixture(sdk):
    for name in ('include/clang/Tooling/Tooling.h', 'include/llvm/Config/llvm-config.h',
                 'lib/libclang-cpp.so', 'lib/libLLVM.so', 'bin/llvm-config'):
        p = sdk / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text('fixture')
    (sdk / 'source-manifest.json').write_text(json.dumps({'revision': PIN}))


class BuildFlow(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'source with spaces'
        self.root.mkdir()

    def args(self, *args):
        return build_editor.arguments(list(args), self.root)

    def test_default_flow_refreshes_stale_configure_before_using_it(self):
        args = self.args('--install')
        old = self.root / 'configure'
        old.write_text('stale system-clang checks')
        observed = []
        def run(command, **kwargs):
            observed.append((command, kwargs))
            if str(self.root / 'autogen.sh') in command:
                self.assertEqual(kwargs['env']['NOCONFIGURE'], '1')
                old.write_text('fresh internal compiler checks')
            if str(old) in command:
                self.assertEqual(old.read_text(), 'fresh internal compiler checks')
                self.assertIn('--with-clang=' + str(args.sdk), command)
            self.assertTrue(kwargs['check'])
            self.assertNotIn('shell', kwargs)
        with patch.object(build_editor, 'prerequisites'), contextlib.redirect_stdout(io.StringIO()):
            build_editor.execute(args, self.root, run)
        self.assertIn('prepare_clang_sdk.py', observed[0][0][1])
        self.assertEqual(observed[-1][0], ['make', 'install'])
        self.assertEqual(observed[-1][1]['cwd'], args.build_dir)

    def test_system_and_disabled_never_prepare_or_fetch_compiler(self):
        for provider in ('system', 'disabled'):
            steps = build_editor.plan(self.args('--clang-provider', provider), self.root)
            self.assertFalse(any('sdk.py' in ' '.join(x['command']) for x in steps))
            self.assertFalse(any(x['command'][0] == 'git' for x in steps))

    def test_meson_reconfiguration_clears_cached_internal_sdk_for_system(self):
        directory = self.root / 'old-build/meson-private'
        directory.mkdir(parents=True)
        (directory / 'coredata.dat').write_text('old settings')
        args = self.args('--backend', 'meson', '--clang-provider', 'system', '--build-dir', str(directory.parent))
        command = build_editor.plan(args, self.root)[0]['command']
        self.assertIn('--reconfigure', command)
        self.assertIn('-Dclang-sdk=', command)
        self.assertIn('-Dclang-provider=system', command)

    def test_existing_in_tree_configuration_is_reused_without_cleanup(self):
        (self.root / 'config.status').write_text('old build')
        self.assertEqual(self.args().build_dir, self.root)

    def test_configuration_failure_stops_before_compile_or_install(self):
        args = self.args('--clang-provider', 'system', '--install')
        commands = []
        def fail(command, **kwargs):
            commands.append(command)
            raise subprocess.CalledProcessError(7, command)
        with patch.object(build_editor, 'prerequisites'), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(subprocess.CalledProcessError):
                build_editor.execute(args, self.root, fail)
        self.assertEqual(len(commands), 1)

    def test_plan_does_not_write_or_execute(self):
        before = list(self.root.iterdir())
        with patch.object(build_editor, 'ROOT', self.root), patch.object(build_editor, 'execute', side_effect=AssertionError('executed')), contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(build_editor.main(['--plan', '--clang-provider', 'system']), 0)
        self.assertTrue(json.loads(output.getvalue()))
        self.assertEqual(list(self.root.iterdir()), before)

    def test_contradictory_and_invalid_arguments_decline(self):
        for argv in (['--jobs', '0'], ['--configure-only', '--install'],
                     ['--', '--with-clang-provider=system']):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                self.args(*argv)


class Preparation(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.sdk = self.root / 'sdk'

    def test_resource_only_install_respects_staged_destination(self):
        include = self.sdk / 'lib/clang/18/include'
        include.mkdir(parents=True)
        (include / 'stddef.h').write_text('builtin header')
        license = self.sdk / 'share/licenses/llvm/LICENSE.TXT'
        license.parent.mkdir(parents=True)
        license.write_text('fixture license')
        stage = self.root / 'staged prefix/share/geany/insight/clang'
        tool = Path(__file__).resolve().parents[1] / 'tools/bundle_clang.py'
        result = subprocess.run([sys.executable, str(tool), '--sdk-lib', str(self.sdk / 'lib'),
                                 '--resources-only', '--resource-destination', str(stage)],
                                env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'), capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((stage / 'resources/include/stddef.h').read_text(), 'builtin header')
        self.assertTrue(list((stage / 'licenses').iterdir()))
        self.assertFalse((stage / 'runtime.json').exists())

    def test_complete_sdk_needs_no_tools_or_downloads(self):
        sdk_fixture(self.sdk)
        with patch.object(prepare_clang_sdk.shutil, 'which', return_value=None):
            paths = prepare_clang_sdk.prepare(self.sdk, root=self.root, run=lambda *a, **k: self.fail('executed'))
        self.assertEqual(paths[0], self.sdk)

    def test_missing_sdk_initializes_only_missing_sources_then_builds(self):
        calls = []
        def run(command, **kwargs):
            calls.append(command)
            if command[0] == 'git':
                for name in ('llvm/CMakeLists.txt', 'clang/CMakeLists.txt'):
                    path = self.root / 'external/llvm-project' / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text('fixture')
            else:
                sdk_fixture(self.sdk)
            self.assertIs(kwargs['stdout'], sys.stderr)
            self.assertNotIn('shell', kwargs)
        with patch.object(prepare_clang_sdk.shutil, 'which', return_value='/tools/tool'), contextlib.redirect_stderr(io.StringIO()):
            prepare_clang_sdk.prepare(self.sdk, root=self.root, run=run)
        self.assertEqual(calls[0][:3], ['git', 'submodule', 'update'])
        self.assertIn('build_external_clang.py', calls[1][1])

    def test_existing_source_edits_are_not_reset_or_checked_out(self):
        source = self.root / 'external/llvm-project'
        for name in ('llvm/CMakeLists.txt', 'clang/CMakeLists.txt'):
            path = source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('local changes')
        calls = []
        def run(command, **kwargs):
            calls.append(command)
            sdk_fixture(self.sdk)
        with patch.object(prepare_clang_sdk.shutil, 'which', return_value='/tools/tool'), contextlib.redirect_stderr(io.StringIO()):
            prepare_clang_sdk.prepare(self.sdk, root=self.root, run=run)
        self.assertEqual(len(calls), 1)
        self.assertEqual((source / 'clang/CMakeLists.txt').read_text(), 'local changes')

    def test_local_compiler_edit_rebuilds_without_reset(self):
        sdk_fixture(self.sdk)
        source = self.root / 'external/llvm-project'
        for name in ('llvm/CMakeLists.txt', 'clang/CMakeLists.txt', '.git', 'clang/edit.cpp'):
            path = source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('local edits')
        calls = []
        def run(command, **kwargs):
            calls.append(command)
            sdk_fixture(self.sdk)
        with patch.object(prepare_clang_sdk.subprocess, 'check_output', side_effect=[PIN, b' M clang/edit.cpp\n']), patch.object(prepare_clang_sdk.shutil, 'which', return_value='/tools/tool'), contextlib.redirect_stderr(io.StringIO()):
            prepare_clang_sdk.prepare(self.sdk, root=self.root, run=run)
        self.assertEqual(len(calls), 1)
        self.assertNotEqual(calls[0][0], 'git')
        self.assertEqual((source / 'clang/edit.cpp').read_text(), 'local edits')

    def test_wrong_revision_sdk_is_not_overwritten(self):
        sdk_fixture(self.sdk)
        manifest = self.sdk / 'source-manifest.json'
        manifest.write_text('{"revision":"other"}')
        with self.assertRaisesRegex(ValueError, 'Refusing'):
            prepare_clang_sdk.prepare(self.sdk, root=self.root, run=lambda *a, **k: self.fail('executed'))
        self.assertEqual(json.loads(manifest.read_text())['revision'], 'other')

    def test_unrecognized_nonempty_sdk_declines_without_writes(self):
        self.sdk.mkdir()
        keep = self.sdk / 'user-file'
        keep.write_text('keep')
        with self.assertRaisesRegex(ValueError, 'Refusing'):
            prepare_clang_sdk.prepare(self.sdk, root=self.root)
        self.assertEqual(keep.read_text(), 'keep')

    def test_incomplete_nonempty_source_declines_without_writes(self):
        source = self.root / 'external/llvm-project'
        source.mkdir(parents=True)
        keep = source / 'user-file'
        keep.write_text('keep')
        with self.assertRaisesRegex(ValueError, 'refusing'):
            prepare_clang_sdk.prepare(self.sdk, root=self.root)
        self.assertEqual(keep.read_text(), 'keep')


if __name__ == '__main__':
    unittest.main()
