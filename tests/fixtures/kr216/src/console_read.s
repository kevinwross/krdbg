; Fixture-only kr216 host-service trampoline: the runtime provides write and
; exit, and the console fixture also reads a byte. Call number 2 is read
; (kr216-isa.md section 13).
.text
.global __krc_read
__krc_read:
    push fp
    mov fp, sp
    ld r7, #2
    sys
    pop fp
    ret
