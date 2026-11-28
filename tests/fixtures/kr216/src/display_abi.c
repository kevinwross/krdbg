// Fixture-side wrappers for the common display ABI over kr216's SYS 5.
//
// The fixture deliberately does not link the C runtime's display wrappers: those
// include <krc/display.h>, and the compiler records the sysroot's absolute path
// in the image's debug data, which would make the committed image depend on the
// host that built it (the drift check rebuilds on both Windows and Linux). The
// block layout below is the 16-bit guest marshalling of the canonical block
// documented in docs/display-abi.md; the fixture is the one place that repeats
// it, and the adapter scenario fails if the ABI moves.
//
// No headers: the fixture avoids the sysroot entirely, like the other kr216
// fixtures. The SYS 5 trampoline lives in targets/kr216/runtime/syscalls.s.

extern int __krc_display(int command, const void *block, unsigned int length);

struct display_string_block {
    const char *address;
    unsigned short length;
};

struct display_line_block {
    char *buffer;
    unsigned short capacity;
};

struct display_click_block {
    unsigned short row;
    unsigned short column;
    unsigned short buttons;
};

int display_puts(const char *text) {
    struct display_string_block block;
    unsigned short length = 0;
    if (text == 0) {
        return -1;
    }
    while (text[length] != 0) {
        length = (unsigned short)(length + 1);
    }
    block.address = text;
    block.length = length;
    return __krc_display(5 /* PUT_STRING */, &block, sizeof block);
}

int display_read_line(char *buffer, int capacity) {
    struct display_line_block block;
    int status;
    if (capacity <= 0) {
        return -1;
    }
    block.buffer = buffer;
    block.capacity = (unsigned short)capacity;
    status = __krc_display(7 /* READ_LINE */, &block, sizeof block);
    if (status >= 0 && status < capacity) {
        buffer[status] = 0;
    }
    return status;
}

int display_get_key(void) {
    return __krc_display(8 /* GET_KEY */, 0, 0);
}

int display_get_click(int *row, int *col, int *buttons) {
    struct display_click_block block;
    const int status = __krc_display(9 /* GET_CLICK */, &block, sizeof block);
    if (status != 0) {
        return status;
    }
    if (row != 0) {
        *row = (int)block.row;
    }
    if (col != 0) {
        *col = (int)block.column;
    }
    if (buttons != 0) {
        *buttons = (int)block.buttons;
    }
    return 0;
}
