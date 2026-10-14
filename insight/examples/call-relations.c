/* Open Relations; enable Follow and Track enclosing function, choose Calls. */
static int increment(int n) { return n + 1; }
static int combine(int n) {
    /* increment(100) here must not appear in Calls. */
    const char *message = "increment(200)";
    (void)message;
    return increment(increment(n));
}
int main(void) { return combine(0); }
