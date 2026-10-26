    // .section __TEXT, __thecodefromhere, regular, pure_instructions
    .text
    .globl  _avkDotProduct

    .balign 4
_avkDotProduct:
    pacibsp  // hint instruction for PAC
    // prologue (0x10 for (fp,lr) + 0x20 for x19,x20,x21,x22, plus we have x9-x15 as scratch)
    // we'll use v16 and v17 to hold temporary results, and 8 32 bit stack slots for the sums
    stp     x29, x30, [sp, #-0x30]!
    mov     x29, sp                // frame chain
    stp     x19, x20, [sp, #0x10]
    stp     x21, x22, [sp, #0x20]  // save regs
    // GS cookie here (not on windows)
    sub     sp, sp, #0x20  // 8 32 bits slots

    // default return value: 0.0f
    mov     v0.s[0], wzr

    // null check
    cmp     w0, wzr
    b.le    avkDocProduct_endFunction
    cmp     x1, xzr
    b.eq    avkDocProduct_endFunction
    cmp     x2, xzr
    b.eq    avkDocProduct_endFunction

    mov     v16.s[0], wzr

    // case less than 4 (put at the end cause it's not the common)
    cmp     w0, #0x4
    mov     w19, wzr
    b.lt    avkDocProduct_shortInput

    // in here we can group by 4, perform SIMD multiplication, add by previous
    // 4-way accumulation, handler remainder.
    // - zero out the 4 lane accumulator on the stack
    stp     xzr, xzr, [x29, #-0x20]
    stp     xzr, xzr, [x29, #-0x10]
    // - w20 = number of 128 bit packets
    mov     w20, #0x4
    sdiv    w20, w0, w20

avkDocProduct_longLoop:
    cmp     w19, w20
    b.ge    avkDocProduct_longLoop_endLoop
    ldr     q16, [x1, w19, sxtw #4]
    ldr     q17, [x2, w19, sxtw #4]
    fmul    v16.4s, v16.4s, v17.4s
    ldur    q17, [x29, #-0x10]
    fadd    v16.4s, v16.4s, v17.4s
    stur    q16, [x29, #-0x10]
    add     w19, w19, #0x1
    b       avkDocProduct_longLoop

avkDocProduct_longLoop_endLoop:
    // - convert w19 from a 128 bit packet index to a single element index
    lsl     w19, w19, #2
    // - horizontal sum of the 4 lanes
    ldr     q17, [x29, #-0x10]
    mov     v16.s[0], wzr
    fadd    s16, s16, s17
    mov     v17.s[0], v17.s[1]
    fadd    s16, s16, s17
    mov     v17.s[0], v17.s[2]
    fadd    s16, s16, s17
    mov     v17.s[0], v17.s[3]
    fadd    s16, s16, s17
    stur    s16, [x29, #-0x10]
    b       avkDocProduct_shortInput_singletonLoop

avkDocProduct_shortInput:
    // cycle through each single element
    // w19 will be the counter
    mov     w19, wzr

avkDocProduct_shortInput_singletonLoop:
    cmp     w19, w0  // was missing, claude pointed that out
    b.ge    avkDocProduct_shortInput_singletonLoop_endLoop
    ldr     s16, [x1, w19, sxtw #2]
    ldr     s17, [x2, w19, sxtw #2]
    fmul    s16, s16, s17
    ldur    s17, [x29, #-0x10]
    fadd    s16, s16, s17
    stur    s16, [x29, #-0x10]
    add     w19, w19, #0x1
    b       avkDocProduct_shortInput_singletonLoop

avkDocProduct_shortInput_singletonLoop_endLoop:
    ldur    s0, [x29, #-0x10]

avkDocProduct_endFunction:
    // epilogue
    add     sp, sp, #0x20  // 8 32 bits slots
    ldp     x21, x22, [sp, #0x20]
    ldp     x19, x20, [sp, #0x10]
    // GS cookie here (not on windows)
    ldp     x29, x30, [sp], #0x30
    autibsp  // hint instruction for PAC
    ret
