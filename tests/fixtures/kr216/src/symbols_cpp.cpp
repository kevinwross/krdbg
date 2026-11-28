// kr216 C++ fixture: a class, a member function's `this`, a member global and a
// source-level local. The adapter must name `this` in the member function's
// frame and resolve the Counter global through its type record.
class Counter {
public:
    int current;
    int bump(int amount);
};

int Counter::bump(int amount) {
    int next = current + amount;
    current = next;
    return next;
}

Counter g_counter = {5};

int main() {
    int step = 3;
    int value = g_counter.bump(step);
    return value;
}
