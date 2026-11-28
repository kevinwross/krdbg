// Guest evidence for the kr232 adapter's DISPLAY: standard output (SYS 1) writes
// the shared text display terminal page and standard input (SYS 2) is served by
// the host line editor. The console page (the debug serial) gets a separate
// marker so a test can prove the two devices are distinct.
//
// The fixture links display_io.s (fixture-only SYS trampolines) instead of the
// full C runtime: the committed image stays small, and the test is about the
// device split rather than about printf.
extern int krc_display_write(int descriptor, const char *buffer, int count);
extern int krc_display_getchar(void);

// The debug serial port is the console page at 0xFFF02000 (kr232-devices.md
// section 4): a guest writes its OUT register directly. Nothing in stdio touches
// it, so a test can prove the two devices are distinct.
#define KR232_CONSOLE_OUT (*(volatile unsigned int *)0xFFF02000u)
static void debug_serial_write(const char *text) {
    for (const char *cursor = text; *cursor != 0; ++cursor) {
        KR232_CONSOLE_OUT = (unsigned int)(unsigned char)*cursor;
    }
}

static int text_length(const char *text) {
    int length = 0;
    while (text[length] != 0) {
        ++length;
    }
    return length;
}

static void display_write(const char *text) {
    (void)krc_display_write(1, text, text_length(text));
}

static int read_line(char *buffer, int capacity) {
    int count = 0;
    int character = krc_display_getchar();
    if (character < 0) {
        buffer[0] = 0;
        return 0;
    }
    while (character >= 0 && character != '\n') {
        if (count + 1 < capacity) {
            buffer[count++] = (char)character;
        }
        character = krc_display_getchar();
    }
    buffer[count] = 0;
    return 1;
}

int main(void) {
    char name[16];
    display_write("NAME? ");
    if (read_line(name, (int)sizeof name) != 1) {
        display_write("BYE\n");
        return 0;
    }
    display_write("HELLO ");
    display_write(name);
    display_write("\n");
    debug_serial_write("DBG");
    return 0;
}
