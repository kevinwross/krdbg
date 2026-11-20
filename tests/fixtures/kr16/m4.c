/* KR16 integration fixture for krdbg M4.
 * Built with krc from KRCompiler at 0defcce or later; see
 * tests/fixtures/kr16/regenerate.ps1. The 0xF000 console block is the only
 * output path, so no libc or host service is linked. */

struct Point {
    int x;
    int y;
};

int global_counter = 7;

int add(int left, int right) {
    return left + right;
}

int main(void) {
    struct Point point;
    point.x = 3;
    point.y = 4;
    int total = add(point.x, point.y);
    global_counter = global_counter + total;
    *(volatile unsigned char*)0xF000 = 'K';
    *(volatile unsigned char*)0xF000 = 'R';
    *(volatile unsigned char*)0xF000 = '1';
    *(volatile unsigned char*)0xF000 = '6';
    *(volatile unsigned char*)0xF000 = '\n';
    int input = *(volatile unsigned char*)0xF004;
    return global_counter + input;
}
