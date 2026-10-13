    .globl  avkInsertionSort
    .type   avkInsertionSort, @function

    .section .note.GNU-stack, "", @progbits

    .text

    // %rdi <- int number of elements to sort in place
    // %rsi <- address of low byte of first 4 byte int element in the unsorted array
avkInsertionSort:
    pushq   %rbp
    movq    %rsp, %rbp

    // %r12 to r31 are temporary registers, push them
    pushq   %r12  # counter 1
    pushq   %r13  # counter 2
    pushq   %r14
    pushq   %r15
    pushq   %rbx

    // stack variables: -4(%rbp) = min
    //                  -8(%rbp) = min pos
    //                 -12(%rbp) = counter1
    //                 -16(%rbp) = counter2
    // total: 16 bytes
    subq    $16, %rsp  // apparently I'm using only 2 vars, I'm removing stack space when tested

    xor     %r12, %r12
    xor     %r13, %r13
    xor     %r14, %r14
    xor     %r15, %r15

    // for starting position to 0
avkInsertionSort_startPositionLoop:
    cmp     %edi, %r12d
    jge     avkInsertionSort_cleanup

    // min <- list[starting position]
    leaq    0(%rsi, %r12, 4), %r14  // r14 = starting_position (recycled as loop counter)
    mov     0(%r14), %r15d          // r15d = min
    mov     %r15d, -4(%rbp)

    // min pos <- starting position
    mov     %r12d, -8(%rbp)
    mov     %r12d, %r14d

avkInsertionSort_startPositionLoop_sublistLoop:  // now r14d is loop counter
    cmp     %edi, %r14d
    jge     avkInsertionSort_startPositionLoop_end

    // if min > number
    mov     -4(%rbp), %r13d
    leaq    0(%rsi, %r14, 4), %r15
    mov     0(%r15), %r15d

    cmp     %r15d, %r13d
    jl      avkInsertionSort_startPositionLoop_sublistLoop_endLoop

    // min <- number, min pos <- starting pos
    mov     %r14d, -8(%rbp)
    mov     %r15d, -4(%rbp)

avkInsertionSort_startPositionLoop_sublistLoop_endLoop:
    inc     %r14d
    jmp     avkInsertionSort_startPositionLoop_sublistLoop
    // end avkInsertionSort_startPositionLoop_sublistLoop

avkInsertionSort_startPositionLoop_end:
    // setting up swap function. r14 holds loop, expired, so free.
    leaq    0(%rsi, %r12, 4), %r14
    mov     0(%r14), %r15d  # tmp
    mov     -4(%rbp), %ebx  # min
    mov     %ebx, 0(%r14)

    mov     -8(%rbp), %r13d
    leaq    0(%rsi, %r13, 4), %r14
    mov     %r15d, 0(%r14)

    // outer loop cleanup
    inc     %r12d
    jmp     avkInsertionSort_startPositionLoop
    // end avkInsertionSort_startPositionLoop

    // cleanup
avkInsertionSort_cleanup:
    popq    %rbx
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12

    leave
    ret
