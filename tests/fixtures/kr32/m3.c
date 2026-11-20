/* KR32 integration fixture for krdbg M3.
 * Built with krc + krld from KRCompiler at 47fa4e5 or later; see
 * tests/fixtures/kr32/regenerate.ps1. No libc: crt0 supplies putchar and the
 * second assembly file supplies getchar, so the image stays small. */

struct Point {
    int x;
    int y;
};

int global_counter = 7;

int add(int left, int right) {
    return left + right;
}

int putchar(int character);
int getchar(void);

int main(void) {
    struct Point point;
    point.x = 3;
    point.y = 4;
    int total = add(point.x, point.y);
    global_counter = global_counter + total;
    putchar('K');
    putchar('R');
    putchar('3');
    putchar('2');
    putchar('\n');
    int input = getchar();
    return global_counter + input;
}
