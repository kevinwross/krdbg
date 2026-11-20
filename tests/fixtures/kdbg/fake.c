struct Point {
    int x;
    int y;
};

int global_counter = 7;

int util_add(int a, int b) {
    return a + b;
}

int _start(void) {
    int total = util_add(1, 2);
    struct Point p;
    p.x = total;
    p.y = global_counter;
    return p.x + p.y;
}
