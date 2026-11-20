; getchar for the M3 fixture. crt0.s supplies putchar and _start.
        .text
        .global getchar
getchar:
        SYSCALL #34
        RET
