; Prints one word through the kernel and exits with 3: the adapter's "step a
; user program through a syscall" test reads this by name. The message sits at
; the image base, which is where the first syscall points.
.globl _start
.text
_start:
  ld r7, #1
  ld r2, #message
  ld r3, #5
  syscall
  ld r7, #0
  ld r2, #3
  syscall
.data
message:
  .byte 104
  .byte 101
  .byte 108
  .byte 108
  .byte 111
