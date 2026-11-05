# EN: Exercise Unicode input, an empty line and EOF in the debugger console.
# RU: Проверяет ввод Unicode, пустой строки и EOF в консоли отладчика.

# EN: Enter any Unicode text at the first prompt.
# RU: Введите любой текст Unicode в первом приглашении.
name = input("Name: ")
# EN: Press Enter without typing anything at the second prompt.
# RU: Во втором приглашении нажмите Enter без ввода текста.
empty = input("Empty line: ")
# EN: Show the text and verify that the second input had length zero.
# RU: Выводим текст и проверяем, что длина второй строки равна нулю.
print(f"name={name!r}, empty_length={len(empty)}")

# EN: Press Send EOF at the last prompt and observe the handler.
# RU: В последнем приглашении нажмите Send EOF и проверьте обработчик.
try:
    input("Send EOF: ")
except EOFError:
    print("EOF received")
