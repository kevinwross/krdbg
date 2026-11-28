// Main unit of the two-unit C++ symbol fixture. It calls into the math unit
// across translation units, owns its own external global, and has a file-scope
// static global and a static function of its own.

#include "symbols_pair_shared.hpp"

int pair_other = 11;

static int main_hidden_total = 3;

static int main_hidden(int n) {
    int adjusted = n + main_hidden_total;
    return adjusted;
}

int main(void) {
    Vec v = {1, 2};
    int base = 4;
    int a = cross_add(v, 3);
    int b = cross_total(a + base);
    int c = main_hidden(b);
    return a + c + pair_other;
}
