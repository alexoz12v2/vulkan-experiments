# Daily Exercise: 11 Oct 2026

The prompt for today is programming insertion sort in x86_64 assembly in the Linux x64 abi.

## Notes

The Insertion sort algorithm is as follows

```
- for starting position in 0..list size - 1:
  - min <- list[starting position]
  - min pos <- starting position
  - for each (number, position) in list[starting position..list size - 1]:
    - if min > number:
      - min <- number
      - min pos <- position
  - tmp <- list[starting position]
  - list[starting position] <- min
  - list[min pos] <- tmp
```

Now, for a remainder of the x86_64 ABI for linux x86_64, which I believe was called System V ABI 
<https://gitlab.com/x86-psABIs/x86-64-ABI/-/tree/master?ref_type=heads>:

If proper prologue and epilogue done, <https://www.cs.nmsu.edu/~jcook/posts/basic-x86-64-assembly/#functiondefs>

```
   8(%rbp) return address
   0(%rbp) previous %rbp value
  -8(%rbp) unspecified
           ...
   0(%rsp) variable size
-128(%rsp) red zone
```

`function entering`
```
pushq %rbp        ;; before: %rbp points to return address
movq  %rsp, %rbp  ;; after: %rbp points to old %rbp value
;; in here you push clobbered registers you want to use

;; ending (without return)
;; pop everything you used my mov'ing them into place
leave  ;; https://www.felixcloutier.com/x86/leave shorthand instruction for RSP = RBP ; POP RBP
ret
```

As noticed, we are using the AT&T form, which is compatible with `clang` compiler.

---

Our function will take 2 arguments

- number of numbers, 4 byte little endian signed integer
- address to first 4 byte signed number of the array, which is expected to be of size specified by second argument. zero or less
  means early return

In AT&T syntax, clang, apparently, a function is just a label. Can I look into compiler explorer? Only after this exercise otherwise
cheating.


BTW, to export a functionas a *global symbol of type function*, you need

```
.globl func 
.type func, @function
```

we know that registers to 2 INTEGER type of arguments (From ABI docs) are `%rdi` (1st) `%rsi` (2nd)
according to <https://rip.hibariya.org/post/calling-c-function-from-assembly-code/>, in order.

---

to access the starting position element in the list, I have to deal with indirect addressing modes for "integer" instructions such as mov.
We know we can use any address as base and index, not like old 8086, but just to be sure, check out <https://paul.bone.id.au/blog/2018/09/05/x86-addressing/>

example with both AT&T and Intel syntax 

```
lea 6(%eax,%ecs,sdfsdfsfsdfsdf), %ebx
lea ebx, [eax + ecx * 4  +6k]
```


---

Wait, does AT&T switch the `cmp` order? Yes, the minuend is the second argument. 

---

In the C part, I'm deactivating the C runtime STDOUT buffering

```c
setvbuf(stdout, NULL, _IONBF, 0);
```

Compilation command

```sh 
clang insertion-sort.s main.c

/usr/bin/ld: warning: /tmp/insertion-sort-e590d1.o: missing .note.GNU-stack section implies executable stack
/usr/bin/ld: NOTE: This behaviour is deprecated and will be removed in a future version of the linker
```

This with clang 23. What gives?

If you don't need an executable stack, you need to pass to the linker `ld` the option 
`-z noexecstack` (Security I Guess) (If you actually need an executable stack `-z execstack`)
- this is for `gcc`, in clang you have `--noexecstack`

If you want to annotate the code for this, you need to add 

``` 
.section .note.GNU-stack,"",@progbits
```

---

First result:

```
Welcome to Insertion sort with x_86_64 assembly!

Starting array:
  
[ 32, 10, 4, 43, 4, 23, 21, 10 ]
Illegal instruction (core dumped)
```

Of course. How into `gdb a.out` and see what is going on

```sh
b avkInsertionSort # breakpoint on our fuction
layout asm # open up split TUI vision between instructions and registers
layout regs # open up also register panel
stepi # execute one instruction
```

after 1 single instruction I'm getting `program received SIGILL: Illegal instruction`

and no assembly available whatsoever

The only idea that comes to my mind is inspect the generated ELF file
code sections and prey that I can find something.

--- 

Apparently, registers `%r16 - %r31` are available only for CPUs supporting the recent intel APX extension, which means that I need to replace them with lower numbers
Need to check intel ABi to find out equivalent temporary registers to substitute

Note: Today I'm on windows, therefore I'll be running from `wsl -d rhel9`

