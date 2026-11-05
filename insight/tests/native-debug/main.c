int main(void)
{
    volatile int marker = 41;
    marker++;
    return marker == 42 ? 0 : 1;
}
