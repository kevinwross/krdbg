#pragma once

// Shared declarations for the two-unit C++ symbol fixture. Keeping the struct
// and the externs here is also the include-path check for a multi-header build.

struct Vec {
    int x;
    int y;
};

extern int pair_global;
extern int pair_other;

int cross_add(Vec v, int n);
int cross_total(int seed);
