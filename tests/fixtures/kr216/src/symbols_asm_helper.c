// kr216 assembly fixture helper. The image's entry is the hand-written
// symbols_asm.s, so the source lines in this image are this function's.
int helper(int seed) {
    int doubled = seed + seed;
    return doubled;
}
