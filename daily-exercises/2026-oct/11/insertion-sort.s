    .globl  avkInsertionSort
    .type   avkInsertionSort, @function

    .section .note.GNU-stack, "", @progbits

    .text

    // %rdi <- int number of elements to sort in place
    // %rsi <- address of low byte of first 4 byte int element in the unsorted array
avkInsertionSort:
    pushq   %rbp
    movq    %rsp, %rbp

    // %r16 to r31 are temporary registers, push them
    pushq   %r16  # counter 1
    pushq   %r17  # counter 2
    pushq   %r18
    pushq   %r19
    pushq   %r20

    // stack variables: -4(%rbp) = min
    //                  -8(%rbp) = min pos
    //                 -12(%rbp) = counter1
    //                 -16(%rbp) = counter2
    // total: 16 bytes
    subq    $16, %rsp  // apparently I'm using only 2 vars, I'm removing stack space when tested

    xor     %r16, %r16
    xor     %r17, %r17
    xor     %r18, %r18
    xor     %r19, %r19

    // for starting position to 0
avkInsertionSort_startPositionLoop:
    cmp     %edi, %r16d
    jge     avkInsertionSort_cleanup

    // min <- list[starting position]
    leaq    0(%rsi, %r16, 4), %r18  // r18 = starting_position (recycled as loop counter)
    mov     0(%r18), %r19d          // r19d = min
    mov     %r19d, -4(%rbp)

    // min pos <- starting position
    mov     %r16d, -8(%rbp)
    mov     %r16d, %r18d

avkInsertionSort_startPositionLoop_sublistLoop:  // now r18d is loop counter
    cmp     %edi, %r18d
    jge     avkInsertionSort_startPositionLoop_end

    // if min > number
    mov     -4(%rbp), %r17d
    leaq    0(%rsi, %r18, 4), %r19
    mov     0(%r19), %r19d

    cmp     %r19d, %r17d
    jl      avkInsertionSort_startPositionLoop_sublistLoop_endLoop

    // min <- number, min pos <- starting pos
    mov     %r18d, -8(%rbp)
    mov     %r19d, -4(%rbp)

avkInsertionSort_startPositionLoop_sublistLoop_endLoop:
    inc     %r18d
    jmp     avkInsertionSort_startPositionLoop_sublistLoop
    // end avkInsertionSort_startPositionLoop_sublistLoop

avkInsertionSort_startPositionLoop_end:
    // setting up swap function. r18 holds loop, expired, so free.
    leaq    0(%rsi, %r16, 4), %r18
    mov     0(%r18), %r19d   # tmp
    mov     -4(%rbp), %r20d  # min
    mov     %r20d, 0(%r18)

    mov     -8(%rbp), %r17d
    leaq    0(%rsi, %r17, 4), %r18
    mov     %r19d, 0(%r18)

    // outer loop cleanup
    inc     %r16d
    jmp     avkInsertionSort_startPositionLoop
    // end avkInsertionSort_startPositionLoop

    // cleanup
avkInsertionSort_cleanup:
    popq    %r20
    popq    %r19
    popq    %r18
    popq    %r17
    popq    %r16

    leave
    ret
