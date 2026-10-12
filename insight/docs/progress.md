# Прототип 0.7: 25 этапов

Каждый этап имеет отдельный коммит в ветке insight/prototype.

| Этап | Реализовано |
|---|---|
| 1 | Cursor position and line navigation |
| 2 | Lua command keyboard accelerators |
| 3 | Save/activate events |
| 4 | Identifier under cursor |
| 5 | Disk source context preview |
| 6 | Open document |
| 7 | Save named document |
| 8 | Filename |
| 9 | Selection ranges |
| 10 | Set selection |
| 11 | Line count |
| 12 | Read line |
| 13 | Replace range |
| 14 | Insert at position |
| 15 | Open event |
| 16 | Close event |
| 17 | Status messages |
| 18 | Error traceback |
| 19 | Command palette |
| 20 | Name/scope/kind search filters |
| 21 | Unsaved buffer preview |
| 22 | Target line highlight |
| 23 | Keyboard result navigation |
| 24 | Project statistics |
| 25 | API capability detection |

Проверки: 8/8 CTest в обычной сборке и с UBSan, тесты дискового кэша и менеджера
расширений с UBSan, JNI на JDK 17 с -Xcheck:jni, компиляция/линковка desktop-модуля
с Geany 2.0/GTK3 и -Wall -Wextra -Werror. GUI-сценарии не проверены в реальном Geany.

APK в этой серии не собирался. Следующие крупные направления: проверка GUI,
LSP/семантические ссылки и иерархии, актуализация индекса, языковые backend-ы,
подсветка/вкладки/навигация Android. Поддержка языков по-прежнему определяется
парсерами Geany; достижение полного уровня Source Insight не заявляется.

## Этап 26: употребления идентификатора в текущем буфере

Добавлен Lua API `editor.occurrences` с байтовыми диапазонами, строками,
колонками, лимитом результатов и полным числом совпадений. Поиск использует
текущий буфер и границы целого слова. Это лексический поиск, включая строки
и комментарии, без семантического разрешения. Тесты покрывают границы слов,
ограничение выдачи, Unicode, CR/LF/CRLF и некорректные аргументы.

## Проверочные сборки Linux x64 и Windows x64

Linux-редактор собран из текущего дерева Geany 2.2 development; добавлен
условный INSIGHT_RELOCATABLE для поиска ресурсов в корне распакованного
комплекта. Windows использует официальный Geany 2.1 x64 с GTK и наш модуль,
собранный MinGW GCC 13.2. Сборщик создаёт модуль и восемь проверочных программ.
Добавлены отдельные профили запуска, пример проверки документа, упаковщик
с исходниками, лицензиями, BUILD-INFO и SHA256SUMS. Linux: восемь core-тестов,
восемь parser-примеров и динамическая загрузка модуля проходят. Windows:
PE x64 и разрешение всех импортов модуля проверены; запуск не подтверждён.
GUI обеих платформ требует проверки на целевых машинах. APK не собирался.
