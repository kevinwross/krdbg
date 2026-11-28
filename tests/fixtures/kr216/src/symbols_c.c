// kr216 C fixture: parameters, locals, globals, an aggregate and the three-register
// console. Symbol tests stop inside add(), outer() and main() to read the whole
// frame chain; the device tests use the console.
//
// crt0 links the C runtime calls this file uses: the fixture declares the host
// write/read traps itself and links the runtime's write trampoline plus the
// fixture-only read trampoline (src/console_read.s).
extern int __krc_write(int fd, const char *buffer, int count);
extern int __krc_read(int fd, char *buffer, int count);

struct Pair {
    int left;
    int right;
};

int g_counter = 4;
struct Pair g_pair = {10, 20};

int add(int a, int b) {
    int sum = a + b;
    return sum;
}

int outer(int start) {
    int extra = add(start, g_counter);
    return extra;
}

int main(void) {
    int base = 3;
    int value = outer(base);
    __krc_write(1, "AB", 2);
    char first = 0;
    char second = 0;
    __krc_read(0, &first, 1);
    __krc_read(0, &second, 1);
    if (first != 0) {
        __krc_write(1, &first, 1);
    }
    return value;
}
