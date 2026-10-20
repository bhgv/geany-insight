# Renumber

Edit → Insert and clipboard → **Renumber…** transforms the exact selection, or the whole document when the selection is empty. Preview does not modify the editor. Apply creates one Undo action and selects the result; Cancel leaves it unchanged.

![Native Renumber dialog in a Linux GUI fixture](media/renumber.jpg)

![Renumber options](media/renumber.svg)

- **Auto** counts up from the first integer; **Start at value** counts up from the entered value; **Offset** adds the value to every integer.
- Output radix: preserve each token's radix, decimal, hexadecimal (`0x`), or octal (leading zero). Input values use decimal, `0x` hex, or leading-zero octal. Signed values are supported.
- Standalone integer tokens only: identifiers, decimal fractions, exponent forms, suffixes and malformed octal/hex candidates are left unchanged. Whitespace, Unicode and line endings are preserved. This is a text operation: integers inside comments and strings are included. A selection is treated as independent text, so select complete numbers.
- Values must fit signed 64-bit integers. Overflow aborts the entire operation. Input and output are limited to 8 MiB; preview displays the first 8000 bytes. Leading zero padding, explicit plus signs and hex letter case are normalized.
- Single ordinary selections only; column selections, multiple selections, virtual space and read-only buffers are excluded. A document edit, selection change, tab switch or closed target while the dialog is open cancels Apply.

Source Insight reference: [Renumber](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Renumber.htm). Column renumbering is not implemented.
