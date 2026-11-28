// Math unit of the two-unit C++ symbol fixture. It owns the external global
// the other unit reads, plus a file-scope static global and a static function
// that only this unit can call.

#include "symbols_pair_shared.hpp"

int pair_global = 40;

static int math_hidden_total = 2;

static int math_hidden(int n) {
    int scaled = n * math_hidden_total;
    return scaled;
}

int cross_add(Vec v, int n) {
    int sum = v.x + v.y + n;
    return math_hidden(sum);
}

int cross_total(int seed) {
    return seed + pair_global;
}
