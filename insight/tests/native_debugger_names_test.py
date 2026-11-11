#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Test shared names, frame scoping and actual GDB type layouts with architecture-specific instruction fixtures.
# RU: Проверяет единые имена, области кадра и настоящие раскладки типов GDB на инструкциях разных архитектур.
import ctypes.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_names_fixture import ABI
from native_debugger_models_test import function

MAIN = r'''
/* EN: Build range at address/size with owned label/scope; frame FALSE denotes a real data range.
 * RU: Создаёт диапазон address/size с собственными label/scope; frame FALSE обозначает настоящие данные. */
static void fixture_range(NativeAssembly *assembly, guint64 address, guint64 size, const char *label, const char *scope)
{
    NativeNamedRange *range = g_new0(NativeNamedRange, 1);
    range->address = address;
    range->size = size;
    range->name = g_strdup(label);
    range->scope = g_strdup(scope);
    range->manual = TRUE;
    g_ptr_array_add(assembly->ranges, range);
}

/* EN: Test all supported operand syntaxes, exact name reuse, type fields, scoped aliases, constants and malformed rules.
 * RU: Проверяет все синтаксисы операндов, единые имена, поля типов, локальные псевдонимы, константы и ошибки правил. */
int main(int argc, char **argv)
{
    NativeAssembly assembly = {0};
    assembly.rules = g_ptr_array_new_with_free_func(native_name_rule_free);
    assembly.ranges = g_ptr_array_new_with_free_func(native_name_range_free);
    assembly.objects = g_ptr_array_new_with_free_func(g_free);
    assembly.registers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    NativeDebug session = {.assembly = &assembly};
    const char *regs[] = {"fp", "rbp", "ebp", "x29", "r11", "r7", "s0", "sp"};
    for (guint i = 0; i < G_N_ELEMENTS(regs); i++)
    {
        guint64 *value = g_new(guint64, 1);
        *value = 0x1000;
        g_hash_table_insert(assembly.registers, g_strdup(regs[i]), value);
    }
    fixture_range(&assembly, 0xff0, 8, "arg: input", "worker");
    fixture_range(&assembly, 0x4000, 32, "packet", "");
    fixture_range(&assembly, 0x4008, 4, "packet.count", "");
    const char *instructions[] = {"mov rax,QWORD PTR [rbp-0x10]", "mov eax,DWORD PTR [ebp-16]",
        "ldr x0, [x29, #-16]", "ldr r0, [r11, #-16]", "ldr r1, [r7, #-16]",
        "lw a0,-16(s0)", "ld a0,-16(s0)", "ldr x0, [sp, #-16]"};
    for (guint i = 0; i < G_N_ELEMENTS(instructions); i++)
    {
        char *notes = native_name_instruction(&assembly, instructions[i], "worker");
        assert(strstr(notes, "arg: input"));
        g_free(notes);
        notes = native_name_instruction(&assembly, instructions[i], "another_worker");
        assert(!strstr(notes, "input"));
        g_free(notes);
    }
    char *notes = native_name_instruction(&assembly, "mov eax,0x2a", "worker");
    assert(strstr(notes, "const 0x2a"));
    g_free(notes);
    notes = native_name_instruction(&assembly, "call 0x4008 <target>", "worker");
    assert(strstr(notes, "packet.count") && !strstr(notes, "const"));
    g_free(notes);
    char *display = native_name_addresses(&session, "0x4008 0x4009 0xff0");
    assert(strstr(display, "0x4008 <packet.count>") && strstr(display, "packet.count+0x1"));
    assert(!strstr(display, "input"));
    g_free(display);
    display = native_name_addresses(&session, "\"0x4008\" 0x4008");
    assert(strstr(display, "\"0x4008\" 0x4008 <packet.count>"));
    g_free(display);
    notes = native_name_instruction(&assembly, "mov eax,[rbp+0x4008]", "worker");
    assert(!strstr(notes, "packet.count"));
    g_free(notes);
    notes = native_name_instruction(&assembly, "mov eax,-0x4008", "worker");
    assert(!strstr(notes, "packet.count"));
    g_free(notes);
    display = native_name_line_addresses(&session, "0x4008  mov eax,[rbp+0x4008]");
    assert(strstr(display, "0x4008 <packet.count>  mov eax,[rbp+0x4008]"));
    g_free(display);
    NativeNamedRange *frame = g_new0(NativeNamedRange, 1);
    frame->frame = frame->manual = TRUE;
    frame->offset = -16;
    frame->name = g_strdup("saved_argument");
    frame->scope = g_strdup("worker");
    frame->base = g_strdup("fp");
    g_ptr_array_add(assembly.ranges, frame);
    notes = native_name_instruction(&assembly, "ld a0,-16(s0)", "worker");
    assert(strstr(notes, "saved_argument"));
    g_free(notes);
    notes = native_name_instruction(&assembly, "ld a0,-16(s0)", "different");
    assert(!*notes);
    g_free(notes);
    assembly.register_names = g_ptr_array_new_with_free_func(g_free);
    native_assembly_register_names(&assembly, "^done,register-names=[\"rax\",\"x0\",\"a0\",\"s0\",\"v0\",\"xmm0\",\"\",\"sp\"]");
    NativeAssemblyWork *registers = g_queue_pop_head(&assembly.work);
    assert(registers && !strcmp(registers->command, "-data-list-register-values x 0 1 2 3 7"));
    native_assembly_work_free(registers);
    native_assembly_register_values(&assembly, "^done,register-values=[{number=\"0\",value=\"0x4000\"},{number=\"1\",value=\"0x4000\"},{number=\"2\",value=\"0x4000\"}]");
    const char *indirect[] = {"mov eax,[rax+0x8]", "ldr w0,[x0,#8]", "lw a1,8(a0)"};
    for (guint i = 0; i < G_N_ELEMENTS(indirect); i++)
    {
        notes = native_name_instruction(&assembly, indirect[i], "worker");
        assert(strstr(notes, "packet.count"));
        g_free(notes);
    }
    assert(!native_name_register("xmm0") && !native_name_register("v0") && !native_name_register("r32"));
    guint64 address;
    assert(!native_name_displace(0, -1, &address));
    assert(!native_name_displace(UINT64_MAX, 1, &address));
    gint64 offset;
    assert(native_name_offset("-0x20", &offset) && offset == -32);
    assert(!native_name_offset("9223372036854775808", &offset));
    NativeNameRule *rule = g_new0(NativeNameRule, 1);
    rule->kind = 1;
    rule->anchor = g_strdup("-0x20");
    rule->name = g_strdup("argument");
    rule->type = g_strdup("fp");
    rule->scope = g_strdup("worker");
    assert(native_name_rule_valid(rule));
    native_name_rule_replace(&assembly, rule);
    NativeNameRule *typed = g_new0(NativeNameRule, 1);
    typed->kind = 2;
    typed->anchor = g_strdup("(char*)$fp-0x20");
    typed->name = g_strdup("stack_packet");
    typed->type = g_strdup("struct Packet");
    typed->scope = g_strdup("worker");
    assert(native_name_rule_valid(typed));
    native_name_rule_replace(&assembly, typed);
    assembly.function = g_strdup("other_function");
    native_assembly_user_rules(&assembly);
    assert(!assembly.work.length);
    g_free(assembly.function);
    assembly.function = g_strdup("worker");
    native_assembly_user_rules(&assembly);
    assert(assembly.work.length == 3);
    for (NativeAssemblyWork *item; (item = g_queue_pop_head(&assembly.work));)
    {
        assert(!strcmp(item->scope, "worker"));
        native_assembly_work_free(item);
    }
    g_free(assembly.function);
    assembly.function = NULL;
    assembly.function = g_strdup("worker");
    NativeAssemblyWork automatic = {.action = 3};
    guint before = assembly.collected;
    native_assembly_metadata(&assembly, &automatic, "^done,variables=[{name=\"input\",arg=\"1\"}]");
    assert(assembly.collected == before + 1 && assembly.work.length == 3);
    for (NativeAssemblyWork *item; (item = g_queue_pop_head(&assembly.work));)
    {
        assert(!item->manual && !strcmp(item->scope, "worker") && !strcmp(item->label, "arg: input"));
        native_assembly_work_free(item);
    }
    automatic.action = 6;
    automatic.label = "arg: input";
    automatic.scope = "worker";
    native_assembly_metadata(&assembly, &automatic, "^done,name=\"auto_pointer\",type=\"struct Packet *\",numchild=\"1\"");
    assert(!assembly.work.length);
    native_assembly_metadata(&assembly, &automatic, "^done,name=\"auto_struct\",type=\"struct Packet\",numchild=\"1\"");
    NativeAssemblyWork *children = g_queue_pop_head(&assembly.work);
    assert(children && !children->manual && children->action == 7);
    native_assembly_fields(&assembly, children,
        "^done,children=[child={name=\"auto_struct.count\",exp=\"count\",type=\"int\",numchild=\"0\"}]");
    native_assembly_work_free(children);
    NativeAssemblyWork *field = g_queue_pop_head(&assembly.work);
    assert(field && !field->manual && !strcmp(field->label, "arg: input.count"));
    native_assembly_metadata(&assembly, field, "^done,path_expr=\"(input).count\"");
    native_assembly_work_free(field);
    for (NativeAssemblyWork *item; (item = g_queue_pop_head(&assembly.work));)
    {
        assert(!item->manual);
        native_assembly_work_free(item);
    }
    automatic.action = 6;
    native_assembly_metadata(&assembly, &automatic, "^done,name=\"auto_dynamic\",type=\"Container\",numchild=\"0\",dynamic=\"1\"");
    NativeAssemblyWork *physical = g_queue_pop_head(&assembly.work);
    assert(physical && physical->action == 15 && strstr(physical->command, "None") && !physical->manual);
    native_assembly_metadata(&assembly, physical, "^done");
    native_assembly_work_free(physical);
    children = g_queue_pop_head(&assembly.work);
    assert(children && children->action == 7 && !children->manual);
    native_assembly_work_free(children);
    assembly.collected = 128;
    automatic.action = 3;
    native_assembly_metadata(&assembly, &automatic, "^done,variables=[{name=\"input\"}]");
    assert(!assembly.work.length && assembly.collected == 128);
    assembly.collected = 0;
    g_free(assembly.function);
    assembly.function = NULL;
    native_assembly_metadata(&assembly, &automatic, "^done,variables=[{name=\"input\"}]");
    assert(!assembly.work.length);
    GKeyFile *profile = g_key_file_new();
    assembly.follow_enabled = TRUE;
    native_name_profile_store(&session, profile);
    GError *error = NULL;
    assert(native_name_profile_validate(profile, &error));
    assert(g_key_file_get_boolean(profile, "assembly-names", "follow", NULL));
    g_key_file_set_string(profile, "assembly-names", "follow", "not-a-boolean");
    assert(!native_name_profile_validate(profile, &error));
    g_clear_error(&error);
    g_key_file_set_boolean(profile, "assembly-names", "follow", TRUE);
    NativeNameRule *loaded = native_name_rule_read(profile, 0);
    assert(loaded && !strcmp(loaded->name, "argument"));
    native_name_rule_free(loaded);
    loaded = native_name_rule_read(profile, 1);
    assert(loaded && loaded->kind == 2 && !strcmp(loaded->scope, "worker"));
    native_name_rule_free(loaded);
    g_key_file_set_string(profile, "assembly-name-0", "scope", "");
    assert(!native_name_profile_validate(profile, &error));
    g_clear_error(&error);
    g_key_file_unref(profile);
    if (argc == 3)
    {
        NativeAssemblyWork work = {.action = (guint)atoi(argv[1]), .label = "object", .scope = "",
                                   .manual = TRUE, .expression = "0x4008"};
        if (work.action == 7)
            native_assembly_fields(&assembly, &work, argv[2]);
        else
            native_assembly_metadata(&assembly, &work, argv[2]);
        for (NativeAssemblyWork *item; (item = g_queue_pop_head(&assembly.work));)
        {
            puts(item->command);
            native_assembly_work_free(item);
        }
    }
    g_ptr_array_unref(assembly.rules);
    g_ptr_array_unref(assembly.ranges);
    g_ptr_array_unref(assembly.objects);
    g_ptr_array_unref(assembly.register_names);
    g_hash_table_unref(assembly.registers);
    return 0;
}
'''


class NativeNames(unittest.TestCase):
    # EN: Compile production helpers against GLib/GIO and check the shared naming model without a display.
    # RU: Компилирует настоящие функции с GLib/GIO и проверяет общую модель имён без дисплея.
    def test_names_and_architectures(self):
        libraries = [ctypes.util.find_library(name) for name in ('gio-2.0', 'glib-2.0')]
        if not all(libraries) or not shutil.which('cc'):
            self.skipTest('Needs cc and GLib/GIO runtimes.')
        directory = Path('src/insight-debuggers/disassembly')
        helpers = '\n'.join(function('src/insight-debuggers/gdb-context.inc', name)
                            for name in ('native_debug_field', 'native_next_tuple'))
        model = Path('src/insight-debuggers/gdb-disassembly-model.inc').read_text()
        code = ABI + (directory / 'state.inc').read_text()
        code += '\ntypedef struct {NativeAssembly *assembly;} NativeDebug;\n' + helpers + model
        for name in ('rules.inc', 'resolve.inc', 'annotations.inc'):
            code += (directory / name).read_text()
        code += function(str(directory / 'names-ui.inc'), 'native_name_profile_store') + MAIN
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'names.c'
            source.write_text(code)
            executable = root / 'names'
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-function', str(source), '-o', str(executable),
                            *['-l:' + name for name in libraries]], check=True)
            subprocess.run([str(executable)], check=True)
            self.check_real_gdb(root, executable)

    # EN: Obtain real type/field MI records without running a target, then feed them through production metadata parsing.
    # RU: Получает настоящие записи MI типов/полей без запуска цели и передаёт их производственному разбору метаданных.
    def check_real_gdb(self, root, helper):
        gdb = os.environ.get('GDB') or shutil.which('gdb')
        if not gdb:
            return
        source = root / 'layout.c'
        source.write_text('struct Inner { int count; }; struct Packet { char tag; struct Inner inner; };\n'
                          'struct Packet object; int main(void) { return object.inner.count; }\n')
        target = root / 'layout'
        subprocess.run(['cc', '-g', '-O0', str(source), '-o', str(target)], check=True)
        commands = ('-gdb-set disassembly-flavor intel\n'
                    '-data-disassemble -s main -e main+8 -- 0\n'
                    '-var-create typed * "*(struct Packet*)&object"\n'
                    '-var-list-children --no-values typed 0 64\n'
                    '-data-evaluate-expression "(unsigned long long)&((struct Packet*)0)->inner"\n'
                    '-gdb-exit\n')
        result = subprocess.run([gdb, '-q', '-nx', '--interpreter=mi2', str(target)], input=commands,
                                text=True, capture_output=True, check=True)
        records = result.stdout.splitlines()
        self.assertTrue(any(line.startswith('^done,asm_insns=') for line in records), result.stdout)
        assembly = next(line for line in records if line.startswith('^done,asm_insns='))
        self.assertNotIn('%rip', assembly)
        fields = next(line for line in records if 'children=[' in line and line.startswith('^done'))
        output = subprocess.check_output([str(helper), '7', fields], text=True)
        self.assertIn('-var-info-path-expression "typed.inner"', output)
        self.assertIn('-var-list-children --no-values "typed.inner"', output)
        offset = next(line for line in records if line.startswith('^done,value='))
        self.assertIn('value="4"', offset)


if __name__ == '__main__':
    unittest.main()
