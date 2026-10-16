# Daily Exercise 14 oct 2026

## Objective

The objective of this exercise is to reproduce the same exact code written for the previous exercise, Insertion sort from x86_64 assembly,
but written in ARM v8/v9 (A profile) 64 bit assebly (aarch64), with Android ABI in mind. Such artifact will be compiled with the 
cross compilation `clang` included in the Android SDK and moved in my phone and executed through `adb push`.

The learning outcome should be

- refresher on ARM assembly (CPU Mode, basic instructions, AAPCS 64 bit calling conventions, ..)
- Clang assembly syntax for macros, declaring sections, ...
- `main.c` will be basically the same as the one from the previous exercise

## Notes

A first detail is that Android requires us to compile, for andoird NDK's `clang` + `adb push`, **Position-Independent Executable**,
produced with `-pie`. Why?

- Asked claude cause this seems too specific a question
  - Since Android 5.0 (API 21), android refuses to execute code which works only on a fixed memory address
    - because of security

**Explain**: When every program runs, every piece of data sits on a memory address. Code refers to it by loading it from memory, therefore 
its address needs to be known. There are two ways in which a memory location (virtual memory) can be referencesd

1. Absolute: "the string is at 0x400078". This works only if the program was loaded where the linker assumed
2. Relative: "the string is 40 bytes after this instruction in this ELF file". This is position independent

This is absolutely necessary for **Address Space Layout Randomization** (ASLR), for which pieces of the executable are not loaded in 
random virtual addresses every run. This makes hacker attacks such as buffer overrun to land in on a well known address much harder

- when working in C, `-pie` for clang is the default, assembly instead, we need to use a combination of `adrp` to load the 4KB page,
  and then add the 12-bit offset with `add`

  - `readelf -h file | grep Type`:  DYN means PIE, EXEC means not PIE
    - you can take llvm-objdump and llvm-readelf from the Android NDK  
    - `readelf -d a | grep FILAGS_1`: DINAMIC means PIE, otherwise, EXEC_P not PIE

  - in x86_64, you write pie by loading the RIP-relative address with `LEA` before executing the MOV. YOu can check its correctness while
    compiling a program with `-fPIE`, as the compiler refuses to use a non PIE assembly source
    | | good | bad |
    |- | - | - |
    | GAS | `lea msg(%rip), %rsi`| `mov $msg, %rsi` |
    | NASM | `lea rsi, [rel msg]` | `mov rsi, msg` |

  - Note: Windows uses a different mechanism, meaning it calls an ALSR uitility with `/DYNAMICBASE`, default on modern `link.exe`, meaning
    that windows patches the absolute addresses while loading the program, but writing RIP relative code is still preferred

    ```asm
    adrp x1, msg           // get KB page msg is on, relative to pc position (here)
    add x1, x1, :lo12:msg  // ...plus the offsett inside that page
    ```

---

From <https://developer.android.com/ndk/guides/other_build_systems>:

- compile with `$NDK/toolchains/llvm/prebuilt/$HOST/bin/clang --target=aarch64-linux-android21 $FILES` so that `minSdkVersion` is 21.
  - I'm on linux amd64, therefore `$HOST="linux-x86_64"`
  - we are focusing on aarch android cause most devices use it. Check 
  - through `adb shell`, you can run `getprop ro.product.cpu.abi` or `uname -m`.
    At compile time on C, you have the Android NDK defined macro `__aarch64__`
--- 

starting writing, let me declaare a `.text` and a labelled function. Probably. yes, using 

```asm
.text 
.globl f 
.type f, @function 

f:
  ret
.size f, .-f
```

Note how we record how many bytes the function takes with the last directive. `ret` is 4 Bytes on ARM (AArch64) and 1 byte in x86_64

Note: in armv7 (32-bit ARM) . Note that that syntax *Breaks* in 32-bit, because @ starts a comment, so use any other form (from claude)

```
.type f, @function     // usual on x86, breaks on ARM 32-bit as @ starts a comment
.type f, %function     // usual on ARM but works on all ELF targets
.type f, #function     // variant
.type f, "function"    // variant
.type f, STT_FUNC      // ELF spec name for function
```

other types are 

| type | `readelf -s` shown | means |
| - | -| -| 
| `%function` | `FUNC` |  code you call |
| `%object` | `OBJECT` |  data: variable, string, table |
| `%gnu_indirect_function` | `IFUNC` |  function whose real version is picked at load time |
| `%notype` | `NOTYPE` |   default when you type nothing |

---

Note ann Apploe assembly: `.type` and  `.size` **Are errros**, unknown directives, cause 
- **Mach-O doesn't have symbol table with type and size**
- leading underscore on a label tells Mach-O that a given symbol is a function

```c
#ifdef __APPLE__
  #define SYM(x) _##x
#else
  #define SYM(x) x
#endif

#ifdef __ELF__
.type SYM(f), %function
#endif
// after function
#ifdef __ELF__
.size SYM(f), .-SYM(f)
#endif
```

**Note**: Since we need to run the C preprocessor first, either you name the file with `*.S`
or add to the compiler the flag `-x assembler-with-cpp`.

---

Now let's talk About AAPCS for AArch64

Note: The document talks about `r0-r30`, while in reality in assembly we refer to these as 
`x0-x30` for the full 64-bit view of the register, wile `w0-w30` for the lower 32-bit view 
of the same register. 
- Unlike x86_64, using lower half **Automatically zeroes out the upper half**

You use registers `x0-x7` to pass parameters which are integer-like and fit the registers.
stack memory is fallback for when you can't fit everything with registers.

Here's <https://devblogs.microsoft.com/oldnewthing/20220824-00/?p=107043/> which shows 
how the prologue and epilogue for a function in Windows on ARM work.
- fp = x29, and it's used as a frame pointer, meaning that we have a *Linked List* of frame pointers each pointing at each other.
- lr = x30, the link register is used as the return address instead of storing it on the stack
- in `stp` saving onto the stack happens right to left, high to low addresses, 
  meaning the leftmost argument is pushed last
- <https://johannst.github.io/notes/arch/arm64.html> other reference 
  for prologue and epilogue

Note: `.text` is synomym of `.section .text, "ax", %progbits`

From Claude:
| flag |                                                meaning                                                |
| - | - |
| a    | allocatable: it takes memory when the program is loaded (unlike debug info, for example)              |
| x    | executable: the code can run (the memory is mapped with the execute permission)                       |
| w    | writable: used for .data and .bss, never for code                                                     |
| M, S | mergeable / strings: used for things like .rodata.str1.1, so the linker can merge duplicate constants |
| G    | belongs to a COMDAT group: used for C++ inline and template functions                                 |



---

from 11 oct


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

---

Dissecting the Windows on ARM prologue with claude 

```
    ; return address protection
    pacibsp

    ; saving registers
    stp     fp, lr, [sp, #-0x30]!
    stp     x19, x20, [sp, #0x10]
    str     x21, [sp, #0x20]

    ; establishing frame chain
    mov     fp, sp

    ; initializing GS cookie
    bl      __security_push_cookie

    ; local variables and outbound parameters
    sub     sp, sp, #0x80

```

What I observed 

- we are preallocating all the stack we need upfront, or at leas part of it.
- `pacibsp` is ARMv8.3 Pointer Authentication (PAC), which is an opt-in hardening feature, and it is in HINT instruction space, so CPUs without it 
 can ignore it
 - Linux, gcc or clang = `-mbranch-protection-pac-ret` default is off so **you need it**
 - Apple arm64e: everywhere
 - Windows: `/guard:signret`

- calling function __security_push_cookie is a Microsoft specific thing for `/GS` helper to avoid stack overrun, linux equivalent of `-fstack-protector`
  Note that these are usually inlined so to use them you usually copy-paste them from elsewhere. (The linux version also has an epilogue)
  - Linux: 
    - load `__stack_chk_guard`
    - store it in the frame
    - before returning reload and compare, call `__stack_chk_fail` if changed

Back to our prologue structure: in the saving registers part, we allocate upfront the space 
**to save registers in reverse order of their numbering `x8-x28`**, while the only exception are x29 and x30 (fp and lr), saved last. same "orientation"
and making the allocation once is the most efficient strategy.

- we can skip the stack overrun, OS specific check (GS security cookie on windows and thread local stack protector on linux), which have parts in both 
  prologue and epilogue, at least for now


Reporting the Windows on ARM epilogue, which is the same steps in reverse, so nuke stack area, check security cookie,  restore all registers, 
pop sp (done at the beginning actually), **HINT instruction** for ARMv8.3 Pointer Authentication (`autibsp`), and return

```
   add     sp, sp, #0x80           ; discard local stack area
    bl      __security_pop_cookie   ; validate and pop GS cookie
    ldr     x21, [sp, #0x20]        ; restore register
    ldp     x19, x20, [sp, #0x10]   ; restore registers
    ldp     fp, lr, [sp], #0x30     ; restore register and pop
    autibsp                         ; validate return address
    ret                             ; return
```

--- 

let us apply what I learned to our example.

- we need 4 saved register values, say `x8-x12`

---

Flags refresher <https://devblogs.microsoft.com/oldnewthing/20220726-00/?p=106898/>: We have the **Application Program Status Register**

| Mnemonic | Meaning | Notes | 
| - | - | - |
| `N` | Negative | Set if the result is negative |
| `Z` | Zero | Set if the result is zero |
| `C` | Carry | Multiple purposes |
| `V` | Overflow | Signed Overflow |
| `Q` | Saturation | Accumulated overflow | 
| `GE[n]` | Greater than or equal to | 4 flags (SIMD) |

Not every instruction automatically sets flags. Some let you append an "S" for that, others do it by default, eg the `cmp` instruction,
which, much like its x86_64 counterpart, does a subtraction without affecting its operands, but sets flags in the **APSR**.


- `cmp` = `subs` where destination is the zero register `xzr`, which is basically a `/dev/zero`

Gemini's summayr of common condition for conditional codes execution (eg `b.cond` or for `csel`)

| code | meaning |
| - | - |
| `eq/ne`| Equal `Z==1` / Not Equal `Z==0`|
| `cs/hs` | Carry set/ unsigned higher or same (`C==1`)|
| `cc/lo`| Carry clear | unsigned lower (`C==0`) |
| `hi/ls`| Unsigned higher (`C==1 && Z==0`)/Unsigned lower or same|
| `ge/lt`| Signed greater or equal (`N==V`)/Signed less than (`N!=V`)|
| `gt/le`| Signed greater than (`Z==0 && N==V`)/ Signed less or equal |

I'd use the APSR related name where applicable

- [] TODO: write on paper a N == V example on why the math checks out
