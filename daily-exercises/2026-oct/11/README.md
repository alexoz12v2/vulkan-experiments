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

