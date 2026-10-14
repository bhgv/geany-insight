/* Place the caret inside "value + 1" below, then press Ctrl+Alt+B repeatedly.
 * Brackets in this comment: } ] ) are ignored. */
int selection_example(int value)
{
    const char *text = "Ignore brackets in strings: ([{}])";
    (void)text;
    if (value > 0)
    {
        return (value + 1) * (value - 1);
    }
    return 0;
}
