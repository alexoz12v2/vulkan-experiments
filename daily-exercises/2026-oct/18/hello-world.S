    .section __TEXT, __maincode, regular, pure_instructions

    .globl  _main
    .p2align 2
_main:
    // prologue without protection cookie or stack allocation. only fp and lr saving
    stp     x29, x30, [sp, #-0x10]!  // save fp(x29) and lr(x30). No extra space needed
    mov     x29, sp                  // frame chain
    // no OS-specific protection (GS cookie) and no register saving

    // want to call _fputs: x0 = pointer to cstring, x1 = FILE*
    // - ___stdoutp from libSystem holds FILE**, we can cross-image reference it with GOT
    // - self PC-relative reference a asciz string
    // - call with stubbed implementation towards a __DATA_CONST the _fputs function
    adrp    x8, msg@PAGE
    add     x0, x8, msg@PAGEOFF  // x0 = msg
    adrp    x8, ___stdoutp@GOTPAGE
    ldr     x8, [x8, ___stdoutp@GOTPAGEOFF]
    ldr     x1, [x8]             // x1 = stdout
    bl      _fputs

    ldp     x29, x30, [sp], #0x10
    ret

    .section __TEXT, __themessage, cstring_literals
    .balign 8
msg:
    .asciz  "Hello Beautiful World\n"

// Compile:
//  xcrun clang -nostdlib -lSystem hello-world.S -o hello
