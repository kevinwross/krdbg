; kr216 assembly fixture: a hand-written _start that calls the C helper and
; exits with its value. The kr216 assembler emits no KDBG records for `.s` text,
; so this image's source lines are the helper's: the assembly side covers the
; instruction stream, its variable-length encodings, and an entry address that
; no function record names.
.text
.global _start
.extern helper

_start:
    push fp
    mov fp, sp
    ld r2, #5
    call helper
    ld r7, #0
    sys

.data
.global seed_word
seed_word:
    .word 1234
