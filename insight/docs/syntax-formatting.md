# Syntax Formatting

**View → Mono Font View** temporarily displays source text in one configurable
font and size, keeping syntax colors. It suppresses per-token bold, italic,
underline and case presentation. The preset is Monospace 12; choose its font in
**Options → Syntax Formatting…**. This temporary switch resets on restart.

The Options dialog saves these independent settings:

- **Use syntax formatting**: off displays the default text font and colors.
- **Use only color formatting**: retains syntax colors and uses the default
  source font, size and emphasis for all tokens.
- **Use no color formatting (monochrome)**: removes syntax color differences and
  uses grayscale default foreground/background; font variations still apply.

Mono Font View overrides all three while active. Turning it off restores the
saved combination. Apply saves changes; Cancel discards them. The mono font must
have a point size from 4 to 96.

These are display modes. They preserve text, caret, selection and Undo. Source
editors and colored previews participate; line-number/brace UI styles and search
or navigation indicators remain separate. New buffers and theme/profile/font
reloads use the active mode. Disabling a mode restores the underlying styles;
profile CSS and theme files are not rewritten. This does not implement the full
Source Insight parent-style hierarchy or import its proprietary style files.

## Визуальные темы Source Insight

«Параметры → Визуальная тема…» предлагает 15 наборов из предоставленного
`default.themes.xml`. Они хранятся отдельно в `styles/<имя>/editor.css` и
`panels.css`; менять CSS можно вручную. «Применить» сохраняет выбор. «Отмена»
оставляет текущий вид. «Текущая тема Geany» возвращает прежние стили профилей.
Тема применяется к открытым и новым редакторам, Context и спискам панелей.
Ручная настройка «Шрифт и цвет» отдельной панели имеет приоритет. Моноширинный
режим и переключатели синтаксического оформления продолжают работать поверх темы.

В `fonts` приложены Noto Sans и Noto Sans Mono с открытыми лицензиями и русскими
буквами, включая Ё/ё и №. Установленные оригинальные шрифты Windows и Verdana
можно использовать без копирования в комплект. Иерархия ParentStyle раскрыта;
все исходные семантические стили сохранены в CSS, но пока применяются только
категории лексера. Полная классификация объявлений/ссылок Source Insight этим
не реализована. Подробности и ограничения: `styles/README.md`, `fonts/README.md`
в исходниках. Новые пользовательские темы размещаются в `styles` каталога
настроек редактора и обнаруживаются при запуске.
