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

Note ann Apploe assembly: `.type` and  `.size` **Are errors**, unknown directives, cause 
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

--- 

Building: I tried 

```sh
~/Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64/bin/clang \
  main.c insertion-sort.S 
```

and got a tons of errors, including `invalid instruction mnemonic pacibsp`

That's because while clang enables Pointer Address Authentication for C compiled files,
raw assembly goes through the assembler, which assumes `arm-v8-a` by defaault unless 
you explicitly tell it that you want PAC, which is for `arm-v8.3-a` or newer. That 
is not a problem because `pacibsp` is part of the HINT instruction address space, meaning 
ignored if not recognized.

Furthermore, we want to specify that we want `--target` as a `aarch64` android. Choose any 
target android API level you want (Android 14 = level 34)
- <https://apilevels.com/> reports that new apps and updates must be at least have 
  `targetSdk` 36+. That doesn't mean that your app doesn't run on older phones. It 
  declares the version you tested with.

| Setting | What it does | Who cares |
| - | - | - |
| `minSdk` | Determines the *oldest Android version that can install the app* | Google play store to filter who can see the app |
| `targetSdk` | tells OS which android version you tested agains, and therefore can apply That API's supported rules | OS of newer phones, and Google Play policy enforcers |
| `compileSdk` | version of Android SDK with which your code is compiled with | developer and compiler |


in code you can bridge the gap, with the following java/kotlin runtime check

```java
if (Build.VERSION_SDK_INT >= 36) {
    // bank
} else {
    // something else
}
```

or in native code 

```C
// rely on clang builtins
if (__builtin_available(android 36, *)) {
    // go
} else {
    // something else
}

// raw Android call
#include <android/api-level.h>

int current_api = android_get_device_api_level(); // -1 if fails
if (current_api >= 36) {
    // go
} else {
    // something else
}
```


Ok, back  to compilation 

--- 

```sh
clang --print-targets # just the first part of the triplets
# target relative
clang --target=aarch64-linux-android34 --print-supported-cpus # exact cpu specific optimization, not general arch
```

for seeing possible values for `--target=` and `--march=`

```sh 
~/Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64/bin/clang --target=aarch64-linux-android34 -march=armv8.3-a main.c insertion-sort.S
```

final compilation command. Now for transfering file on the device: <https://developer.android.com/tools/adb#copyfiles>

---

What claude code has to say about android's file system 

- Android uses a Linux-based file system using `ext4` and `F2FS` (Flash-Friendly file system)
  for its internal storage and `exFAT` and `FAT32` for removable SD cards
- System partitions fundamental
  - `/boot`: Kernel abnd RAM disk needed to start the device
  - `/system`: Core operating system, pre-installed apps, GUI interface
  - `/recovery`: recovery console and used for factory resets and updates
  - `/data`: holds user-specific data, installed applications, databases, and system 
    settings. to see requires superuser, otherwise you can see an application data using 
    `run-as` to impersonate a given package
  - `/cache`: Frequently accessed app data for performance are moved here. An app can
    use it to its advantage with `getCacheDir()`.
  - `/misc`: hardwar-specific settings
  - `/sdcard`: Symbolic link or mount point to **shared user storage**
  - `/sd-ext`: Extended storage partition occasionally used in custom roms

For our purpose to execute a manually compiled ELF executable, what we need to know

- we could put it in the Downloads folder `/sdcard/Download`, but it wouldn't be executable
  - find the mount point of current directory (say download folder) with `df -P .`
  - grep it inside `/proc/mounts`
  - TODO: `grep -EnH "/storage/emulated" /proc/mounts` study its output, beyond scope of 
    assembly exercise
    - I found the Filesystem to be of type `fuse`, meaning Filesystem in Userspace, ie.
      each file access goes through a user-space driver. This is how Scoped storage
      is implemented for app
    - there is the `noexec` flag in this mount, so I can't execute an ELF in the 
      download folder

`/data` is root only access, But the only part we can use is `/data/local/tmp`, which 
belongs to the shell session.

It still is contained in the **Opaque Binary Blob** (obb), which is for app related 
storage, but we can access it. Can we query which obb belongs to "us"?

```
sunstone:/data $ ls
ls: .: Permission denied
1|sunstone:/data $ df -P .
Filesystem       512-blocks     Used Available Capacity Mounted on
/dev/block/dm-50  217083512 46877184 169944184      22% /storage/emulated/0/Android/obb
sunstone:/data $ cd local/tmp
sunstone:/data/local/tmp $ llllls
/system/bin/sh: llllls: inaccessible or not found
127|sunstone:/data/local/tmp $ ls
APKPure_3.20.7803_apkpure.com.apk  sky.com.example.u_1_first.sha1
sunstone:/data/local/tmp $ df -P .
Filesystem       512-blocks     Used Available Capacity Mounted on
/dev/block/dm-50  217083512 46886280 169935088      22% /storage/emulated/0/Android/obb
```


**Actually No**, `/data/local/tmp` does not belong or live inside **OBB**. This is an 
artifact of **Android's Storage Sandbox namespaces**. Basically, the android shell 
doesn't have permission to see the actual mountspace for `/data`. Therefore, it 
falls back to the closest matching visible mount on the same physical partition,
which is `/dev/block/dm-50`

Cut it here, just run 

```sh
adb push a.out /data/local/tmp
```

---

```
❯ adb push a.out /data/local/tmp
a.out: 1 file pushed, 0 skipped. 105.0 MB/s (7624 bytes in 0.000s)
❯ adb shell 
$ cd /data/local/tmp
$ chmod +x a.out                                                                                                                                                                                                                                                                                      
$ ./a.out
Hello and welcome to Android ABI aarch64 insertion sort with assembly!
  Before sorting: 
[ 32, 4, 7, 18, 0, -4, 43, 32, 1 ]Illegal instruction
```

Alright, how in in debugging

1. check if `a.out` has DWARF symbols
  - `llvm-readelf -S a.out | grep debug`
  - `llvm-dwarfdump --debug-info a.out | less`

What I've executed 

```sh
~/Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf \
  -S a.out | grep debug
```

nothing, so I need to recompile by adding `-g -O0`

```sh 
 ~/Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64/bin/clang \
   --target=aarch64-linux-android34 -march=armv8.3-a -g -O0 main.c insertion-sort.S
```

after that 

```sh 
"$NDK_BIN/llvm-dwarfdump" --debug-info a.out
```

shows to me all compilation units, with many interesting details, eg symbols, compilation
flags, and links to absolute path files on this disk. Important for debugging I guess,
though I'm sure there's commands to manipulate such info and redirect it

## Remote debugging with `lldb`

For convenience: 
```sh
NDK_BIN="/home/alessio/Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64/bin"
NDK="/home/alessio/Android/Sdk/ndk/28.2.13676358"
```

1. We need to open up a server in the phone with the native debugging protocol, which means 
  exposing a tcp port in our local network

  - Push `lldb-server`. With `find "$NDK" -name lldb-server` you find all supported target triplets
    choose to `adb push` the one for aarch64. Of course, put it in `/data/local/tmp` as previously established
  - in a phone dedicated terminal start the server
    ```sh 
    adb shell chmod +x /data/local/tmp/lldb-server
    adb shell "cd /data/local/tmp && ./lldb-server gdbserver :5039 ./a.out"
    ```

    After that last instruction the program waits for a debugger to connect before executing
    the first instruction. If you have leftovers from previous sessions use `adb shell pkill lldb-server`

  - open up a tcp port with `adb forward tcp:5039 tcp:5039`

  - start `lldb` as usual. *This means you need a local copy of the debug executable* 
    `"$NDK_BIN/lldb" a.out`

2. Inside `lldb`

---

Actually, Tying 

```sh
"$NDK_BIN/lldb" a.out
/home/alessio/Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64/bin/lldb: error while loading shared libraries: libpython3.11.so.1.0: cannot open shared object file: No such file or directory
```

which means that we need to properly setup `LD_LIBRARY_PATH` to point towards NDK packaged shared libraries (and `PYTHONHOME`)

The NDK includes a shell script in the `bin` directory, `lldb.sh`, which sets up the 
environment for us. Now back to debugging

---

2. Inside `lldb`
  - `"$NDK_BIN/lldb.sh" a.out`
  - set remote execution instead of local: `gdb-remote 5039`
  - If you can't find sources cause DWARF recorded absolute paths, you can redirect them 
    ```
    (lldb) settings set target.source-map <build-dir-in-DWARF> <whatever-you-want>
    ```

Table from claude code(<https://lldb.llvm.org/use/tutorial.html>)


|                   Goal                   |                          Command                           |
| - | - |
| Breakpoint on a function or line         | b main, b insertion-sort.S:<line>, b <symbol>              |
| Run / continue                           | c                                                          |
| Step one source line (into / over calls) | s / n                                                      |
| Step one instruction (into / over calls) | si / ni                                                    |
| Show the instructions around the PC      | dis -p, or dis -s $pc -c 10                                |
| Mix source and assembly                  | dis -m -n <function>                                       |
| Show source after every stop             | settings set stop-disassembly-display always               |
| Registers                                | register read, register read x0 x1 sp pc                   |
| Memory                                   | memory read -s4 -fd -c9 <addr> (9 × 4-byte decimal values) |
| C variables                              | frame variable, p <expr>                                   |
| Call stack                               | bt                                                         |
| Watch a value change                     | watchpoint set expression -- <addr>                        |
| Look up what's at an address             | image lookup -a <addr>                                     |
| Run a command at every stop              | target stop-hook add -o "register read x0 x1 x2"           |


lldb stops on its own when the program receives a signal. 
At that point `bt`, `dis -p` and `register read` tell you where 
it stopped and what state the program was in.

Without a debugger, you can

- Crash dump: Android's crash handler logs one for every native crash. 
  Read it with `adb logcat -b crash`. It contains the faulting PC, 
  the registers and a backtrace.
- Turn crash addresses into source lines: `llvm-addr2line -e a.out -f -C <addr>`. 
  Use the offset from the backtrace, not the runtime address.
- Symbolize a whole crash log: `ndk-stack -sym . -dump <logfile>`.

---

from logcat I can see that, after adding the missing `ret` instruction (causing a `SIGILL`),
we get a `SIGSEGV` (ret code 139), my favourite

```
10-17 11:35:36.789 31755 31755 F libc    : Fatal signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x6000000008 in tid 31755 (a.out), pid 31755 (a.out)
10-17 11:35:36.865 31759 31759 F DEBUG   : *** *** *** *** *** *** *** *** *** *** *** *** *** *** *** ***
10-17 11:35:36.865 31759 31759 F DEBUG   : Build fingerprint: 'Redmi/sunstone_eea/sunstone:14/UKQ1.240624.001/OS2.0.6.0.UMQEUXM:user/release-keys'
10-17 11:35:36.866 31759 31759 F DEBUG   : Revision: '0'
10-17 11:35:36.866 31759 31759 F DEBUG   : ABI: 'arm64'
10-17 11:35:36.866 31759 31759 F DEBUG   : Timestamp: 2026-10-17 11:35:36.853456703+0200
10-17 11:35:36.866 31759 31759 F DEBUG   : Process uptime: 1s
10-17 11:35:36.866 31759 31759 F DEBUG   : Cmdline: /data/local/tmp/a.out
10-17 11:35:36.866 31759 31759 F DEBUG   : pid: 31755, tid: 31755, name: a.out  >>> /data/local/tmp/a.out <<<
10-17 11:35:36.866 31759 31759 F DEBUG   : uid: 2000
10-17 11:35:36.866 31759 31759 F DEBUG   : signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x0000006000000008
10-17 11:35:36.866 31759 31759 F DEBUG   :     x0  0000000000000009  x1  0000007ffc3121dc  x2  0000000000000001  x3  ffffffffffffffff
10-17 11:35:36.866 31759 31759 F DEBUG   :     x4  ffffffffffffffff  x5  0000007ffc311b31  x6  0000000000000031  x7  7f7f7f7f7f7f7f7f
10-17 11:35:36.866 31759 31759 F DEBUG   :     x8  0000000000000020  x9  0000000000000009  x10 0000000000007c0b  x11 0000007892dbcd70
10-17 11:35:36.866 31759 31759 F DEBUG   :     x12 0000000000000167  x13 0000007ffc311184  x14 0000000000000000  x15 0000000000000010
10-17 11:35:36.866 31759 31759 F DEBUG   :     x16 0000000000000001  x17 0000007892da7f18  x18 00000078977f0000  x19 0000006021ccc948
10-17 11:35:36.866 31759 31759 F DEBUG   :     x20 0000007ffc312298  x21 0000000000000001  x22 0000007ffc312288  x23 0000000000000000
10-17 11:35:36.867 31759 31759 F DEBUG   :     x24 0000000000000000  x25 0000000000000000  x26 0000000000000000  x27 0000000000000000
10-17 11:35:36.867 31759 31759 F DEBUG   :     x28 0000000000000000  x29 00000020fc312200
10-17 11:35:36.867 31759 31759 F DEBUG   :     lr  0000006000000008  sp  0000007ffc3121c0  pc  0000006000000008  pst 0000000060001000
10-17 11:35:36.867 31759 31759 F DEBUG   : 1 total frames
10-17 11:35:36.867 31759 31759 F DEBUG   : backtrace:
10-17 11:35:36.867 31759 31759 F DEBUG   :       #00 pc 0000006000000008  <unknown>
```

Trying to take that `pc` address with `addr2line`?

- `fault addr` and `pc` in register dump are ASLR runtime addresses.
- meaningg you can only look at the backtrace cause these are relative to the start of the binary

Thing is that our backtrace is consistently empty! going back with debugger

stepping back into debugging and placing a breakpoint on `ret`, the command to print an array is 

```
memory read -s4 -fd -c9 $x1
```

- means: size of element 4 bytes, format decimal, count 9

or 
```
p *(int (*)[9])$x1
```

--- 

So 

- `ret` makes me crash, meaning the Link Register is broken
- the array is unsorted and garbled at the end, meaning there is some logic errors

for the first issue:

`register read $lr` at the beginning of the function we get
```
 lr = 0x00000055555599b8  a.out`main + 112 at main.c:21:3
```

After the prologue,  (`sub sp, sp, #0x08`)

`memory read -s8 -fx -c8 $sp`
```
0x7fffffe928: 0x0000007ff41e5e30 0x0000007fffffe9a0
0x7fffffe938: 0x00000055555599b8 0x0000005555559948
0x7fffffe948: 0x0000007fffffea38 0x0000000000000001
0x7fffffe958: 0x0000007fffffea28 0x0000007fffffe97c
```

Translated:

```
    sp -> 0x7fffffe928: 0x0000007ff41e5e30 
    fp -> 0x7fffffe930: 0x0000007fffffe9a0 -> checked with `register read $fp`, this is old fp
          0x7fffffe938: 0x00000055555599b8 -> should be x29 (lr) -> it is, it's main.c:21:3
          0x7fffffe940: 0x0000005555559948 -> should be x19
          0x7fffffe948: 0x0000007fffffea38 -> should be x20
          0x7fffffe950: 0x0000000000000001 -> should be x21 -> checked with `register read $x21`
          0x7fffffe958: 0x0000007fffffea28 -> should be x22
old sp -> 0x7fffffe960: 0x0000007fffffe97c
```

On the ret breakpoint

```
(lldb) memory read -s8 -fx -c8 $sp
0x7fffffe960: 0x0000007fffffe97c 0x0000000900000000
0x7fffffe970: 0x0000007ff41e5e30 0x0000002000000009
0x7fffffe980: 0x0000000700000004 0x0000000000000012
0x7fffffe990: 0x0000002bfffffffc 0x0000002000000020
(lldb) memory read -s8 -fx -c8 $sp-0x38
0x7fffffe928: 0x0000007ff41e5e30 0x00000020ffffe9a0
0x7fffffe938: 0x0000005500000008 0x0000005555559948
0x7fffffe948: 0x0000007fffffea38 0x0000000000000001
0x7fffffe958: 0x0000007fffffea28 0x0000007fffffe97c
(lldb) register read $lr -> lr = 0x0000005500000008
```

so the epilogue is also correct, as the `$lr` value is effectively from address 
`0x7fffffe938`. This means that some instruction is erroneously writing to it.

**`$fp` + 0x8/0x4 is wrong**: we need to subtract to get to stack variables, otherwise 
we dirty the `$lr`

---

This fixes the first problem of crashing. Now we are left with the broken algorithm

```sh
>adb shell /data/local/tmp/a.out
Hello and welcome to Android ABI aarch64 insertion sort with assembly!
  Before sorting: 
[ 32, 4, 7, 18, 0, -4, 43, 32, 1 ]
  After sorting: 
[ 32, 4, 7, 18, 0, -4, 43, 32, 32 ]
```

I really don't want to go step by step. Let's try to setup breakpoint commands on each 

- outer loop start -> print starting position
- inner loop end -> print current array state after swap

`inner`
```
b insertion-sort.S:50
breakpoint comand add <id>
register read w19
continue
DONE
```

`before swap`
```
b insertion-sort:86
breakpoint command add <id>
register read w20 w8
register read w21 w19
continue
DONE
```

`outer`
```
b insertion-sort.S:91
breakpoint command add <id>
p *(int(*)[9])$1
continue
DONE
```

`parray 9 (int*)$x1`  prints in multiple lines, while `p *(int(*)[9])$1` in one line

---

What I found is that every swap writes to the last element of the array 
(may be coincidence), and never does the second write of the swap procedure

I'd add a new breakpoint in the inner loop which does the swap. Given the starting array 


```c
  int32_t arr[] = {32, 4, 7, 18, 0, -4, 43, 32, 1};
```

the first comparison should be between 32 and 4, therefore at line `csel`, we should have 
- `$w21` <- 4 (0x04)
- `$w22` <- 32 (0x20)

**Breakthrough**: In the inner loop, I've never done a `cmp`, therefore we were branching based on the NZCV flags from the 
counter comparison in the for loop. After fixing that, I've removed the `csel dest, true, false, cond` instruction cause I didn't 
need it, and it now works.

---

### Final: `mac-os/ios` execution

I don't consider this complete. I'd like to try and execute this for MacOS Apple Silicon (M4), and on my iPad (no iPhone for now)


