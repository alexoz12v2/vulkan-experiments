#ifdef __APPLE__
#define SYM(x) _##x
#else
#define SYM(x) x
#endif

    //.text
    .section .text, "ax", %progbits

    .globl  SYM(avkInsertionSort)

#ifdef __ELF__
    .type   SYM(avkInsertionSort), %function
#endif

    .balign 4  // Align code on a 4 byte boundary (useless cause there is no data before this, but just to know)

    // input params:
    //   w0 <- signed integer, number of elements inside the int32_t array
    //   x1 <- address, 4-byte aligned, of the first element (low) in the int32_t array
    // return value: nothing
SYM(avkInsertionSort):
    // prologue: save fp and lr, and take 4 saved registers (eg x19-x22) (Skipping OS specific stack protector)
    // 1. Return address Protection
    pacibsp
    // 2. Saving registers. Layout bottom-top: , fp (x30), lr (x29), x19, x20, x21, x22
    // stp low, high, address
    stp     fp, lr, [sp, #-0x30]!  // note: writeback = allocation of stack memory (48 bytes = 6 registers)
    stp     x19, x20, [sp, #0x10]  // 16 bytes added here: space for 2 registers
    stp     x21, x22, [sp, #0x20]
    // 3. Frame chain: fp -> old fp -> older fp -> .. And the old fp is on top of stack (low addr)
    mov     fp, sp
    // 4. Local variable allocation. In our case, we'll need
    // [fp, #-0x04] <- int32_t, min, initialized each iteration
    // [fp, #-0x08] <- int32_t, min pos, initialized each iteration
    // Total: #0x08 bytes
    sub     sp, sp, #0x08

    // ---------
    // w19 <- outer loop counter
    // w20 <- inner loop counter
    // w0  <- numel, input
    // x1  <- address to first, input
    // from x8 to x15 are not preserved, so we can use them as needed.

    // for starting position in 0 .. list size - 1
    mov     w19, wzr
    mov     w20, wzr
avkInsertionSort_outerLoopStart:
    sub     w20, w0, 0x01
    cmp     w19, w20
    b.ge    avkInsertionSort_outerLoopEnd

    // min <- list[starting position], min pos <- starting position
    // use w20 as working thing. we can use x19 cause 32 bit operating instructions clear upper part -> LSL #0x2
    // or use 220, but give it UXTW or SXTW, which zero or sign extend and then perform the left shift
    ldr     w20, [x1, w19, SXTW #0x2]
    stp     w19, w20, [fp, #-0x08]

    // for each (number, position) in list[starting position .. list size - 1]
    add     w20, w19, #0x01
avkInsertionSort_outerLoop_innerLoopStart:
    cmp     w20, w0
    b.ge    avkInsertionSort_outerLoop_innerLoopEnd

    // compare and csel: w21 for current element, w22 for running min
    ldr     w21, [x1, w20, SXTW #0x2]
    ldr     w22, [fp, #-0x04]
    cmp     w21, w22  // current vs min

    // csel    w21, w22, w21, lt  // csel doesn't consume flags. Removed cause we don't need it, but it is an interesting instruction
    b.ge    avkInsertionSort_outerLoop_innerLoop_afterStrMinPos
    // otherwise store min pos too (unlike ARM 32 bit, AArch64 doesn't support all conditional instructions, if compiles then pseudo)
    stp     w20, w21, [fp, #-0x08]
avkInsertionSort_outerLoop_innerLoop_afterStrMinPos:

    add     w20, w20, #1
    b       avkInsertionSort_outerLoop_innerLoopStart

avkInsertionSort_outerLoop_innerLoopEnd:
    // swap: fetch min and min pos, start pos <- w19
    ldp     w21, w20, [fp, #-0x08]
    // element at starting pos
    ldr     w8, [x1, w19, SXTW #0x2]
    // store everything
    str     w20, [x1, w19, SXTW #0x2]
    str     w8, [x1, w21, SXTW #0x2]

    // looping end of avkInsertionSort_outerLoopStart
    add     w19, w19, #0x01
    b       avkInsertionSort_outerLoopStart
avkInsertionSort_outerLoopEnd:

    // ---------

    // epilogue: undo stack allocation, restore saved registers, validate return address (skip OS Stack protector)
    // 1. free stack space for variables
    add     sp, sp, #0x08
    // 2. restore saved register. Note how argument passing is the same as stp, doesn't invert
    ldp     x21, x22, [sp, #0x20]
    ldp     x19, x20, [sp, #0x10]
    ldp     fp, lr, [sp], #0x30  // note: postfix is always with writeback (otherwise why add after the access)
    // 3. Pointer address authentication (HINT intruction from ARMv8.3)
    autibsp

    ret

#ifdef __ELF__
    .size   SYM(avkInsertionSort), .-SYM(avkInsertionSort)
#endif
