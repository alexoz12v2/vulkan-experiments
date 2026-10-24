    .section __TEXT, __thecode, regular, pure_instructions
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
    // slots
    // [x29 + 0x08] | 1st acc, 0 | remainder start and end, also final result
    // [x29 + 0x0c] | 1st acc, 1 |
    // [x29 + 0x10] | 1st acc, 2 |
    // [x29 + 0x14] | 1st acc, 3 |
    // [x29 + 0x18] | 2nd acc, 0 | --
    // [x29 + 0x1c] | 2nd acc, 1 |
    // [x29 + 0x20] | 2nd acc, 2 |
    // [x29 + 0x24] | 2nd acc, 3 |

    // return 0 if something wrong
    mov     v0.s[0], wzr

    // null check
    cmp     w0, wzr
    b.le    avkDocProduct_endFunction
    cmp     x1, xzr
    b.eq    avkDocProduct_endFunction
    cmp     x2, xzr
    b.eq    avkDocProduct_endFunction

    // case less than 4 (put at the end cause it's not the common)
    mov     v16.s[0], wzr
    cmp     w0, #0x04
    mov     w19, wzr
    b.lt    avkDocProduct_shortInput

    // in here we can group by 4, perform SIMD multiplication, add by previous
    // 4-way accumulation, handler remainder.
    // - zero out the whole accumulators
    stp     xzr, xzr, [x29, #0x08]
    stp     xzr, xzr, [x29, #0x18]
    mov     w20, #0x04
    sdiv    w20, w0, w20  // w0 is the number of 128 bit packets in the array

    // long loop setup
avkDocProduct_longLoop:
    cmp     w19, w20
    b.ge    avkDocProduct_longLoop_endLoop

    // vector fetch (Note: 128 bit fetch with scalar register view, wants shift of 4)
    // TODO Anki
    ldr     q16, [x1, w19, SXTW #0x04]
    ldr     q17, [x2, w19, SXTW #0x04]
    fmul    v16.4s, v16.4s, v17.4s
    ldr     q17, [x29, #0x08]  // We'll probably crash for alignment
    fadd    v16.4s, v16.4s, v17.4s
    str     q16, [x29, #0x08]  // We'll probably crash for alignment

    add     w19, w19, #0x01
    b       avkDocProduct_longLoop

avkDocProduct_longLoop_endLoop:
    // - convert w19 from a 128 bit packet index to a single element index
    lsl     w19, w19, #0x02
    // - remainder management. Currently 4 accumulated values in low bits of v16
    ldr     q17, [x1, w19, SXTW #0x04]
    mov     v16.s[0], wzr

    // hadd with manually fetching low element, inserting next in low lane, fetching it again
    fadd    s16, s16, s17
    ins     v17.s[0], v17.s[1]
    fadd    s16, s16, s17
    ins     v17.s[0], v17.s[2]
    fadd    s16, s16, s17
    ins     v17.s[0], v17.s[3]
    fadd    s16, s16, s17

    // now store in stack slot
    str     s16, [x29, #0x08]

avkDocProduct_shortInput:
    // cycle through each single element
    // - w19 will be the counter
    mov     w19, wzr
    // - received from s16/v16.s[0] the remainder or zero, put it in [x29, 0x08]
    str     s16, [x29, #0x08]

avkDocProduct_shortInput_singletonLoop:
    // w19 is either zero or points to the index of start remainder
    b.ge    avkDocProduct_shortInput_singletonLoop_endLoop

    // load single to s16 and s17, multiply to s16, load prev to s17, add to s16, stora back
    ldr     s16, [x1, w19, SXTW #0x2]
    ldr     s17, [x2, w19, SXTW #0x2]
    fmul    s16, s16, s17
    ldr     s17, [x29, #0x08]
    fadd    s16, s16, s17
    str     s16, [x29, #0x08]

    // inc and jump
    add     w19, w19, #0x1
    b       avkDocProduct_shortInput_singletonLoop

avkDocProduct_shortInput_singletonLoop_endLoop:
    ldr     s0, [x29, #0x08]  // final result

avkDocProduct_endFunction:
    // epilogue
    add     sp, sp, #0x20  // 8 32 bits slots
    ldp     x20, x21, [sp, #0x20]
    ldp     x19, x20, [sp, #0x10]
    // GS cookie here (not on windows)
    ldp     x29, x30, [sp], #0x30
    autibsp  // hint instruction for PAC
    ret
