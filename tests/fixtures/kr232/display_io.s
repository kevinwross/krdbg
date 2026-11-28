; Fixture-only kr232 SYS trampolines for the DISPLAY stdio test. The fixture
; runs kernel-mode under crt0.s, so the kernel-only `sys` hypercall is available:
; SYS 1 descriptor 1 writes standard output to the terminal page and SYS 2
; descriptor 0 reads the host line editor. These are the same host services the
; C runtime's stdio uses; linking only this small file keeps the committed image
; small and avoids the full runtime.
.text
.globl krc_display_write
krc_display_write:
  ld r7, #1
  sys
  ret

.globl krc_display_getchar
krc_display_getchar:
  ld r7, #2              ; display read: R2 descriptor, R3 address, R4 count
  ld r2, #0              ; the display input stream
  ld r3, #krc_display_char
  ld r4, #1
  sys
  cmp r2, #1
  beq krc_display_ready
  cmp r2, #0
  beq krc_display_eof
  bmi krc_display_getchar ; -1: the source is open and nothing is typed yet
krc_display_ready:
  ld r5, #krc_display_char
  ldbu r2, [r5]
  ret
krc_display_eof:
  ld r2, #-1
  ret

.data
krc_display_char:
  .byte 0
