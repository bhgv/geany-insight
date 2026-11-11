#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Catch compound C assignment operators split by source formatting.
# RU: Находит составные операторы присваивания C, разорванные форматированием.
import re
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOKEN = re.compile(
    r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|'
    r'\s+|>>=|<<=|\+=|-=|\*=|/=|%=|&=|\|=|\^=|'
    r'\+\+|--|->|<<|>>|<=|>=|==|!=|&&|\|\||'
    r'[A-Za-z_]\w*|[0-9]\w*|.', re.S)
OPERATORS = {'+', '-', '*', '/', '%', '&', '|', '^', '<<', '>>'}


# EN: Return lines where source has separated operator and assignment tokens.
# RU: Возвращает строки source с раздельными токенами оператора и присваивания.
def split_assignments(source):
    source = source.replace('\\\n', '')
    previous = ''
    for token in TOKEN.finditer(source):
        text = token.group()
        if text.isspace() or text.startswith(('/*', '//')):
            continue
        if text == '=' and previous in OPERATORS:
            yield source.count('\n', 0, token.start()) + 1
        previous = text


class AssignmentTokens(unittest.TestCase):
    # EN: Verify real broken assignments and valid increment lvalues are distinguished.
    # RU: Проверяет различение разорванных присваиваний и корректных выражений с инкрементом.
    def test_recognizes_compound_tokens(self):
        self.assertEqual(list(split_assignments(
            'bytes+ = length; flags| = bit; enabled & = selected;')), [1, 1, 1])
        self.assertEqual(list(split_assignments(
            'bytes += length; flags |= bit; enabled &= selected; *w++ = *r++;')), [])
        self.assertEqual(list(split_assignments('value +/**/= 1;')), [1])

    # EN: Ignore strings, comments and valid backslash-newline token continuations.
    # RU: Исключает строки, комментарии и корректное продолжение токена через перенос строки.
    def test_ignores_noncode_and_continuations(self):
        source = 'char *s = "+ ="; /* & = */\n// | =\nx+\\\n=1;'
        self.assertEqual(list(split_assignments(source)), [])

    # EN: Check all editor C sources and include fragments for the formatting regression.
    # RU: Проверяет исходники C и включаемые фрагменты редактора на повторение ошибки форматирования.
    def test_editor_sources_have_intact_assignment_operators(self):
        failures = []
        for path in sorted((ROOT / 'src').rglob('*')):
            if path.suffix not in ('.c', '.h', '.inc') or not path.is_file():
                continue
            source = path.read_text(encoding='utf-8')
            failures.extend(str(path.relative_to(ROOT)) + ':' + str(line)
                            for line in split_assignments(source))
        self.assertEqual(failures, [], 'Split C assignment operators:\n' + '\n'.join(failures))


if __name__ == '__main__':
    unittest.main()
