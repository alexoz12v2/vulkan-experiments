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

---

From <https://developer.android.com/ndk/guides/other_build_systems>:

- compile with `$NDK/toolchains/llvm/prebuilt/$HOST/bin/clang --target=aarch64-linux-android21 $FILES` so that `minSdkVersion` is 21.
  - I'm on linux amd64, therefore `$HOST="linux-x86_64"`
  - we are focusing on aarch android cause most devices use it. Check 
  - through `adb shell`, you can run `getprop ro.product.cpu.abi` or `uname -m`.
    At compile time on C, you have the Android NDK defined macro `__aarch64__`
