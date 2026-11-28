// Guest evidence for the kr216 adapter's DISPLAY: display_puts writes the shared
// text display, display_read_line reads a host-edited line from it, and
// display_get_key/display_get_click poll the same device's queues. The debug
// serial console gets a separate marker so a test can prove the two devices are
// distinct.
//
// It links the fixture-side ABI wrappers (src/display_abi.c) and the runtime's
// SYS trampolines only: no libc and no sysroot headers, so the committed image
// is reproducible on Windows and Linux.
extern int display_puts(const char *text);
extern int display_read_line(char *buffer, int capacity);
extern int display_get_key(void);
extern int display_get_click(int *row, int *col, int *buttons);

// The console device: SYS 1 writes the debug serial stream, not the display.
extern int __krc_write(int descriptor, const char *buffer, int count);

int main(void) {
    char name[16];
    display_puts("NAME? ");
    // The first call arms the host editor and answers NOT_READY; poll until the
    // completed line (or end of input) arrives.
    int line = display_read_line(name, (int)sizeof name);
    while (line == -2) {
        line = display_read_line(name, (int)sizeof name);
    }
    if (line >= 0) {
        display_puts("HELLO ");
        display_puts(name);
        display_puts("\n");
    } else {
        display_puts("BYE\n");
    }

    // The adapter's send_key / send_click forwarding reaches these queues.
    int key = display_get_key();
    while (key < 0) {
        key = display_get_key();
    }
    display_puts("KEY ");
    // A single printable key proves forwarding; 8-bit ASCII is the cell range.
    if (key >= 32 && key < 127) {
        char text[2];
        text[0] = (char)key;
        text[1] = 0;
        display_puts(text);
    } else {
        display_puts("?");
    }
    display_puts("\n");

    int row = 0;
    int column = 0;
    int buttons = 0;
    int status = display_get_click(&row, &column, &buttons);
    while (status < 0) {
        status = display_get_click(&row, &column, &buttons);
    }
    display_puts("CLICK\n");

    __krc_write(1, "DBG", 3);
    return 0;
}
