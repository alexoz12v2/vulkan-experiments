# Daily Exercise 18 oct 2026

## Objective 

The objective of this exercise is to write a couple of ARMv8A (AArch64) assembly function programmed to run on Linux ABI and Apple ABI, prioritizing apple devices,
while learning about command line tools for Apple Mobile devices development, in line with previous exercise, in which we started learning about `adb`.

The program in particular consists of similar projects we've done before, with a slight difference
- The assembly object should this time define and export 2 functions, one to execute the dot product between two equally sized vectors of 32-bit floating point values
  - the second assembly function should instead print a fixed message (pie practice) onto the console terminal (meaning OS system call involved, meaning that 
    we'd need to link against some POSIX functions)

Testing will involve attaching my iPad 6 to run such code, meaning we'll need to learn a few bits of Apple mobile devices file systems, such as we did with Android
(will be explored further later on)

## Notes

Let me start with a barebones `main.c`, which, while will link against llvm's C runtime (`libc`), it will not use `printf`, instead we'll opt for POSIX equivalent functions
meaning we need the `write` POSIX function.

Apparently, you can type `man 2 intro` or `man 3 intro`, let's see how readable they are. 

**After Looking**: From what I've understood from `man 3 intro`, `-lSystem` is the linking flag we are looking for? let's analyse the 3 files this guide points me towards

| file | content |
| - | - |
| `/usr/lib/libSystem.dylib`        |  the main System library |
| `/usr/lib/libSystem_debug.dylib`  |  the main System library compiled with debug support |
| `/usr/lib/libSystem_profile.dylib`|  the main System library compiled for profiling |


These are all *Mach-O* executable files, therefore `otool` is what we need.

Actually, the aforementioned files don't exist in my machine! Why?
From macOS 11 (Big sur) `libSystem` no lognger points to a real file. It still works if a program references it though, thanks to the 
**dyld (dynamic linker) shared cache**. <https://gitlab.kitware.com/cmake/cmake/-/work_items/20863>

To link against the real system libraries, you need to use Apple Command line, as the SDK contains *Text-Based Stub* `.tbd` files. The stubs tell hte linker which symbols
are available and how macOS hooks them up to the shared cache at runtime
- tdb -> Text-Based Stub definitions is an anki card for sure

```sh
ls $(xcrun --show-sdk-path)/usr/lib/libSystem.tbd
```

very well, it seems that there is a declared symbol for write in there 

```sh 
cat $(xcrun --show-sdk-path)/usr/lib/libSystem.tbd 2>&1 | grep -E "\b_write\b"
                       'R8289209$_write' ]
                       _write, '_write$NOCANCEL', _writev, '_writev$NOCANCEL' ]
```

Note: Apple's clang takes care of setting the `-syslibroot` to the SDK path automatically. Otherwise, you need to add `-syslibroot $(xcrun --show-sdk-path)` to your linker.

To actually see the true file, you'd need to get it from the Shared Cache first, which should be `/System/Volumes/Preboot/Cryptexes/Os/System/Library/dyld/`, not that 
cached data is easy to reverse engineer and we are not interested in that

Note that this `dyld` runtime resolution allows you to swap `libSystem` "flavour" with an environment variable.
- `DYLD_IMAGE_SUFFIX=_debug ./program` will force `dyld` to load the `_debug` version of `libSystem`, even if you linked with `-lSystem` and not `-lSystem_debug` 
- profile version is hardly used nowadays, cause more modern tools such as Xcode tracing capabilities and/or DTrace handle profiling

--- 

Now that we know how to link against these symbols, we need to know which built-in headers are in the SDK so that function signatures come to us.

- `man 2 ...`: **System Calls** (user side prepping) direct requests to XNU Kernel.
- `man 3 ...`: **Library Functions** provided by `libc`. Friendly wrappers for section 2.

So, to search for a page by description, you need to use `apropos`. If you know the name of the function you want to read about, then `man 2 write`, for example

**Wait**, that man page tells me that I need to add `-lc` to linker, but `libSystem.tbd` exports `_write` too. What is going on? Ask gemini:
- **On MacOS** `libc` actually doesn't exist. Apple's linker acts as a translator. `-lc`, `-lpthread`, `-lm` all map to `lSystem` (Or maybe there are symlinks)
- Note that both `clang`s actually inject `-lSystem` automatically, unless you add `-nostdlib`. In fact, try to compile something written using a `man 2` function with 
  that

As for headers, 

```sh 
find $(xcrun --show-sdk-path) -type d -name include
```

will show you the location. BTW, clang should already be configured to look inside the sdks folders without you specifying `-isysroot`
The difference is that LLVM clang uses fixed directories for its discovery while Apple Clang talks with `xcrun`. On my machine 

```
~
❯ clang --version
Homebrew clang version 21.1.2
Target: arm64-apple-darwin25.0.0
Thread model: posix
InstalledDir: /Volumes/ExtData/alessioext/.brew/Cellar/llvm/21.1.2/bin
Configuration file: /Volumes/ExtData/alessioext/.brew/etc/clang/arm64-apple-darwin25.cfg

~ 
❯ echo | clang -E -Wp,-v -
clang -cc1 version 21.1.2 based upon LLVM 21.1.2 default target arm64-apple-darwin25.0.0
ignoring nonexistent directory "/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk/usr/local/include"
ignoring nonexistent directory "/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk/Library/Frameworks"
#include "..." search starts here:
#include <...> search starts here:
 /Volumes/ExtData/alessioext/.brew/Cellar/llvm/21.1.2/lib/clang/21/include
 /Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk/usr/include
 /Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk/System/Library/Frameworks (framework directory)
 /Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk/System/Library/SubFrameworks (framework directory)
End of search list.
# 1 "<stdin>"
# 1 "<built-in>" 1
# 1 "<built-in>" 3
# 469 "<built-in>" 3
# 1 "<command line>" 1
# 1 "<built-in>" 2
# 1 "<stdin>" 2


~
❯ /usr/bin/clang --version
Apple clang version 17.0.0 (clang-1700.3.19.1)
Target: arm64-apple-darwin25.0.0
Thread model: posix
InstalledDir: /Volumes/ExtData/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin

~
❯ echo | /usr/bin/clang -E -Wp,-v -
clang -cc1 version 17.0.0 (clang-1700.3.19.1) default target arm64-apple-darwin25.0.0
ignoring nonexistent directory "/Volumes/ExtData/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/local/include"
ignoring nonexistent directory "/Volumes/ExtData/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/Library/Frameworks"
#include "..." search starts here:
#include <...> search starts here:
 /usr/local/include
 /Volumes/ExtData/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/17/include
 /Volumes/ExtData/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include
 /Volumes/ExtData/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/include
 /Volumes/ExtData/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/Library/Frameworks (framework directory)
 /Volumes/ExtData/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/Library/SubFrameworks (framework directory)
```

- So Apple Clang is capable of extracting stuff from xcode, while LLVM Clang looks into static folders, which may or may not exist

We'll use Apple Clang for our experiments

--- 

`-nostdlib` verified. If you add `-lSystem` or leave it default it works. **Always prefer more explicit syntaxes**

```
/usr/bin/clang -nostdlib main.c
Undefined symbols for architecture arm64:
  "_write", referenced from:
      _main in main-6e0351.o
ld: symbol(s) not found for architecture arm64
clang: error: linker command failed with exit code 1 (use -v to see invocation)
```

---

Now getting onto mobile. I know a little bit of `adb`. We need to do the same for apple.

- Everything goes thorugh `xcrun`, which locates the active **Xcode toolchain**
- Two main tools from Xcode toolchain: `simctl` for simulators, and `devicectl` for physical devices

try out

```sh 
xcrun simctl help
xcrun devicectl help
```

By listing help, I figured out the `adb devices` equivalent, which is 

```sh
xcrun devicectl list devices
```

There is no `adb shell` equivalent though.

That is blocking. We don't want to Jailbreak our device, so how can we execute a small CLI program? Ask claude code about it:

### Answer from Claude Code about equivalent for my terminal program loop on `adb`, without jailbreak

Basically we need a small app.

- There is no `/data/local/tmp` equivalent
- there is no shell without jailbreak 
- You cannot execute an **unsigned binary**
- we need an app bundle

At minimum, an `*.app` bundle is a folder containing our binary and a `Info.plist`, so that `devicectl` can installl it, launch it, redirect stdout/stderr to our terminal.

1. *One time setup*
  - **Developer Mode** on your device, under Settings > Privacy & Security > Developer Mode (needs a restart). Mobile device must trust the macOS machine so that it shows up to 
    `xcrun devicectl list devices` (which it does, the mobile device asks you when you connect it)
    - Developer mode appeaars only if your device picks up that you might be a developer. The easiest way is to open a blank xcode project and build it on your device.
      in my case, I just created a "Swift Playground" project type.
    - the build required me to log in in my apple account by selecting the package and filling in "Signing and Capabilities" developer team, cause otherwise we didn't have a signed 
      package. Added my personal account as development team.
    - then, inside the device, after the first build, you'd need to go inside the device's settings, under Generals > VPN & Device Management, and authorize your own 
      developer team signature as Trusted Certificate
    - `security find-identity -v -p codesigning` can let you see the Developer Signature that xcode created for us. **You'll need it later**
  - Get a **Provisioning Profile**: free Apple ID to sign code, which expires in 7 days though. Perfect for exercises.
    - in Xcode, create a throwaway iOS App project with a Bundle ID
    - Build it once to the iPad, so that Xcode registers the device and downloads the profile in `~/Library/Developer/Xcode/UserData/`

Here's the procedure to create a complete application and run it inside the device (From claude code)

#### Build, Sign, Install, Run

```bash 
UDID=<from xcrun devicectl list devices, Identifier>
BID=it.alexoz12cgdev.asstest # bundle identifier
TEAM=<team id from security find-identity -v -p codesigning>
APP=build/asstest.app
mkdir -p $APP

# 1. Compile for iOS (not macOS) so LC_BUILD_VERSION says platform IOS
# - selects Apple Clang and properly sets -march and --target automatically (TODO: Check)
# - choosing minimum iOS 17 (correspponding to iPadOS ?? TODO)
xcrun -sdk iphoneos clang -arch arm64 -minphoneos-version-min=17.0 -g -O0 main.c $APP/asstest

# 2. Minimal Info.plist (you create it without comments)
cat  > $APP/Info.plist <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <!-- Bundle Identifier and bundle-relative path to the executable -->
  <key>CFBundleIdentifier</key><string>$BID</string>
  <key>CFBundleExecutable</key><string>asstest</string>

  <!-- Whatever we called the app bundle-->
  <key>CFBundleName</key><string>asstest</string>

  <!-- Always APPL -->
  <key>CFBundlePackageType</key><string>APPL</string>

  <!-- Some Versions -->
  <key>CFBundleVersion</key><string>1</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>

  <!-- Minimum OS version we required at compile time -->
  <key>MinimumOSVersion</key><string>17.0</string>

  <!-- Device families we support. I assume 2 stands for mobile? -->
  <key>UIDeviceFamily</key><array><integer>2</integer></array>
</dict></plist>
EOF

# 3. Embed profile + entitlements (get-task allows to let a debugger attach)
cp "path/to/profile.mobileprovision" $APP/embedded.mobileprovision
cat > build/ent.plist <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>application-identifier</key><string>$TEAM.$BID</string>
  <key>com.apple.developer.team-identifier</key><string>$TEAM</string>

  <!-- Attach debugger TODO: Goes to an Anki card for sure -->
  <key>get-task-allow</key><true />
</dict></plist>
EOF

codesign -f -s "Apple Development: <email/name from security find-identity -v -p codesigning>" --entitlements build/ent.plist $APP

# 4. Install and run with stdio attached to your terminal 
xcrun devicectl device install app --device $UDID $APP
xcrun devicectl device process launch --device $UDID --console --terminate-existing $BID
```
- TODO: To consider this exercise complete, after writing my app bundle my myself, you should write a zsh script to autogenerate these, by taking secrets from environment.
  - can take ~5 days

To emulate an `lldb-server` (Still claude code):
- `--start-stopped` to `process launch` command. Then you'd need to use XCode's Attach Debugger. Wait, can't We do it from command line? (See below?)

Limitations
- *Sandbox*: Apps run inside their own isolated container. Meaning no `fork`/`exec`, `getenv("HOME")` and `getenv("TMPDIR)` will give you app-specific folders.
- *Watchdog* (TODO Anki): Without a `UIApplicationMain` (Implemended by `@main` `App` class in *SwiftUI*) your process gets killed after ~20s
- *Least friction*: Using a throwaway Xcode SwiftUI playground project, then deleting all files and adding `main.c` and my own stuff seems to be the fastest way.
  - Note: `osascript -e 'version of application "Xcode"'`

Finally, to check your Provisioning Profiles, which you need to copy in the app bundle. **Why?**
- **code signature** signed the binary, proves that it hasn't been modified since signing
- **provisioning profile** says whether teh signer is allowed to run the app on a given device with the requested *entitlements*
  - the Development Certificate is issued by Apple, but it doesn't grant code execution alone, it need the profile so that the Certificate is accepted, otherwise the signature
    isn't trusted.

- in the App store, you need a profile to submit the app, but the copy users download doesn't contain it, because after review Apple resigns your application with their own certificate
  and removes the embedded profile

To check your embedded profile (TODO Anki) run

```sh 
# the name of your provisioning profile is contained in this folder <- NAME
cd "~/Library/Developer/Xcode/UserData/Provisioning Profiles/" 
# outputs its contents to stdout, which you'd redirect to a p.plist
security cms -D -i <NAME>.mobileprovision 
```

**IMPORTANT**: A free provisioning profile is per application

#### Reproducible Debug compile and run after first configuration

I'll write a script which compiles the exercise as an application, given environment variables DEVICE, BID, TEAM, as well with documentation on how to define them. Its basis comes from 
claude code. Doesn't matter, the core here is to learn assembly, and we still didn't touch a single line of assembly code.

Since I'm planning multiple exercises, I'll write `ios-run.sh` in `daily-esercises` folder directly

---

### What is the difference bewteen Team, Identity, Profile

- Team: Who you are to Apple 
- Identity: Key with which you sign your code 
- Profile: Apple's permission contract for one app

- A **team** is identified by your team id, can be either personal (Attached to your Apple ID), or paid. It owns everything else, certificates, registered devices, profiles. One Apple ID
  can belong to multiple teams
- **Identity** is a private Key in your Mac's keychain plus the Certificate Apple Issued for it. `codesign -s` uses it and `security find-identity` lists it.
  - says to which team it belongs to
  - private key never leaves your Mac
  - Different kinds of identities: *Apple Development* for your own device, *Apple Distribution* for App Store, and MacOS *Developer ID* for distribution outside of App Store.
- **Provisioning Profile** is an Apple signed file which grants permissions for *One App ID* (TODO Anki)
  - which identities may sign the app (so which certificates)
  - which entitlements the app may claim 
  - which devices it can run on 
  - when it expires

---

Claude code on *Decoiding provisining profile fallback*:

1. Does the profile file exist? Xcode 16+ stores profiles in a different folder from older versions, so check both:
ls ~/Library/Developer/Xcode/UserData/Provisioning\ Profiles/ | grep 29060ef3
ls ~/Library/MobileDevice/Provisioning\ Profiles/ | grep 29060ef3

2. Decode the profile and check its contents:
```sh
P=~/Library/Developer/Xcode/UserData/Provisioning\ Profiles/29060ef3-df59-4af1-a3a1-67690cd6de2a.mobileprovision
security cms -D -i "$P" > /tmp/profile.plist
/usr/libexec/PlistBuddy -c "Print :Name" /tmp/profile.plist
/usr/libexec/PlistBuddy -c "Print :TeamIdentifier" /tmp/profile.plist
/usr/libexec/PlistBuddy -c "Print :Entitlements:application-identifier" /tmp/profile.plist
/usr/libexec/PlistBuddy -c "Print :ExpirationDate" /tmp/profile.plist
/usr/libexec/PlistBuddy -c "Print :ProvisionedDevices" /tmp/profile.plist
```
Check that:
- application-identifier is M2GV3NW9SQ.it.alexoz12cgdev.DevUnlock (or a wildcard that covers it).
- The expiration date is in the future.
- Your device UDID is in ProvisionedDevices.

3. Check that the signing certificate in the profile is in your keychain:
```sh
security find-identity -v -p codesigning
```
The profile's DeveloperCertificates must include one of those. To compare fingerprints:
```sh
/usr/libexec/PlistBuddy -c "Print :DeveloperCertificates:0" /tmp/profile.plist | openssl x509 -inform DER -noout -fingerprint -sha1
```
The SHA-1 should match one in the find-identity list.

4. Check the device ID:
DEVICE=8C15457B-... is a CoreDevice UUID, not a UDID. ProvisionedDevices lists UDIDs, so look up the UDID separately:
```sh
xcrun devicectl list devices
xcrun xctrace list devices
devicectl shows the CoreDevice identifier, and the UDID is the hardware identifier in the xctrace output. Check that the UDID is in the profile.
```

---

Example which worked for me (with some information removed)

```sh
DEVICE=<> \
BID=it.alexoz12cgdev.DevUnlock \
TEAM=<> \
IDENTITY="Apple Development" \
PROFILE=~/Library/Developer/Xcode/UserData/Provisioning\ Profiles/<>.mobileprovision \
../../ios-run.sh main.c
08:45:05  Acquired tunnel connection to device.
08:45:05  Enabling developer disk image services.
08:45:06  Acquired usage assertion.
App installed:
• bundleID: it.alexoz12cgdev.DevUnlock
• installationURL: file:///private/var/containers/Bundle/Application/<device id here>/cli.app/
• launchServicesIdentifier: unknown
• databaseUUID: <device id here>
• databaseSequenceNumber: 1340
• options:
08:45:07  Acquired tunnel connection to device.
08:45:07  Enabling developer disk image services.
08:45:07  Acquired usage assertion.
Hello World from Apple!
Launched application with it.alexoz12cgdev.DevUnlock bundle identifier.
Waiting for the application to terminate…
The app terminated with the exit code 0.
```

We can finally start the exercise!

---

Here some notes fused from the two links about AAPCS for AArch64. Basically, on top of the knowledge 
coming from standards AAPCS calling conventions, Apple devices make their own customizations on top 
of it, eg 128-bit values which should be passed in integer registers, (this rule applies to all 
16-byte aligned types actually, as integer values bigger than that go on the stack)
- (Standard AAPCS): stored in 2 registers, the first one of which (lower index) stores the low part 
  in little endian machines, has its index rounded up to an even value. So, if the current 
  "integer register assignment counter" (called NGRN) is on an odd value, eg `x1`, a 128-bit value 
  will be assigned to `x2-x3`
- (Apple): 128-bit values stored in integer registered are always rounded up to an **odd** value,
  so, in the same situation as before, the assignment would be `x1-x2`.

We therefore need a primer on floating point instructions and on how can we call `-lSystem` functions
from assembly.

--- 

Reading from book in <https://cs140e.sergio.bz/docs/ARMv8-A-Programmer-Guide.pdf>, I've picked up that PC-relative addressing covers a ±4GB range. How since 
we've got 12 bits in the page offset, meaning that, unsigned offset gets us anywhere in a **4KB page**? We've got 21 bits, *signed*, remaining to specify a full 4KB range.

```arm64
adrp x0, sym               // x0 = page address of sym
add  x0, x0, :lo12:sym     // x0 += low 12 bits of sym's address
```

- `adrp`: `(PC + ~0xFFF) + (imm21 << 12)` where `imm21` is the page index of the 4KB memory page containing `sym`, computed by assembler
- `:lo12:sym`: `sym & 0xFFF`

In a Load you can actually skip add and use the offsetting capability 

```arm64
adrp x0, sym
ldr x1, [x0, :lo12:sym]  // offset must be a multiple of 8 (8-byte aligned) for an x register
```

Note that if you examine the object file, the assembler doesn't know where `sym` will end up in memory, until the **linker** applies the final address computation. So what will 
the object file contain. a **Relocation record** (TODO Anki) against symbol `sym`, which will then be filled by linker. There are two kinds of relocation record for an aarch64 target

| Instruction | Relocation | What the linker Writes | 
| - | - | - | 
| `adrp x0, sym` | `R_AARCH64_ADR_PREL_PG_HI21` | `(Page(sym) - Page(PC)) >> 12` into 21-bit field | 
| `add x0, x0, :lo12:sym` | `R_AARCH64_ADD_ABS_LO12_NC` | `sym & 0xFFF` into 12-bit field | 
- note how the page index depends on the current value of PC.
- decoding `R_AARCH64_ADD_ABS_LO12_NC`: 
  - `R_AARCH64`: AArch64 ELF relocation namespace
  - `ADD`: it is a patch for an `add` instruction 
  - `ABS`: hte value of hte patch is absolute, meaning not relative to PC (`PREL` is the opposite)
  - `LO12`: only low 12 bits are used
  - `_NC`: *No Check*, meaning if value doesn't fit, don't throw a Linker error, but truncate the number.
- another variant for the second bit is `R_AARCH64_LOST64_ABS_LO12_NC`, where `LOST64` means use bits 11:3, as the offset is 8-aligned (`ldr x, ...`)

What happens when PIE is involved?
- *Link Time* (Static Linker): Knows the layout *Within the Image*, meaning Section A is N Bytes 
  from Section B, but **doesn't know the base address at which full image is loaded**
- *Load Time* (Dynamic Linker, `ld.so`, `linker64`, `dyld`): Picks a base address for the whole image, 
  which ASLR randomizes, and patches all relative offsets depending on it
  - What gets randomized: (**Anki TODO**) 
    - executable image and each shared library loaded in virtual address space
    - the stack 
    - the heap 
    - `mmap` regions
  - *All Image Segments* (`.text`, `.rodata`, `.data`, `.got`) **are fixed** from the 
    randomized base of the image (or their shared library)
    - Note that this means that PC-relative addressing **cross-image** is impossible. We'll see **GOT** in a moment.

PC-Relative code does nothing at load time, as `adrp/add` encodes **distance from symbol**, which doesn't change

**Mach-O uses a different syntax**, because of course it does. For *Intra-Image* Access, PC-relative addressing looks like this:

```arm64 
adrp x0, _sym@PAGE         // remember: all global symbols, whether functions or variables, have a leading underscore
add x0, x0, _sym@PAGEOFF
```

why different syntaxes? Different assemblers, different objsect formats constraint, 
different symbol conventions and linking models

### Cross-page and distance > 4GB

**Cross Image Access** (access to a symbol defined in another shared library) cannot be reached with normal 
PC relative addressing, cause the 4KB page in which the symbol resides changes in virtual address space due to 
ASLR.

The **GOT** (*Global Offset Table*) (ELF/Mach-O, IAT on windows) is an array of pointers contained in the *Image File*
- TODO Anki card 
The **Dynamic Linker** fills these slots with each symbol's real address. How?
- whenever you use syntax for a GOT access, the **Compiler** creates a *Dynamic relocation slot* of a specific 
  kind. In particular `R_AARCH64_ADR_GOT_PAGE` / `LD64_GOT_LO12_NC` (adrp + load) on ELF, or `GOT_LOAD_PAGE21`
  on Mach-O
- **Static Linker** emits a *Dynamic Relocation* slot that tells the loader how to fill it.

| Slot Kind | Dynamic reloc (ELF) | Filled With | 
| -  | - | - | 
| Symbol looked up by name | `R_AARCH64_GLOB_DAT` | address of the exported symbol found | 
| Function called via PLT | `R_AARCH64_JUMP_SLOT` | address of hte function, or a resolver stub until bound |
| Symbol in this same image | `R_AARCH64_RELATIVE` | `base + addend`, no lookup (so if you Use GOT syntax when you shouldn't ELF lets you get away with it) | 
| Thrad-local variable | `TLS*` relocations | TLS offset or descriptor |


- What is **PLT**? All in due time

*Not every GOT slot involves another library*. A global defined in your own `*.so` compiled with `-fPIC` 
(shared library equivalent of `-fPIE`) still goes through the GOT by default, because ELF allows another 
module ot override (preempt) it, meaning the new shared module takes its place in virtual memory space.
- using `-fvisibility=hidden`, `-Bsymbolic`, `-fno-semantic-interposition` removes that possibility and 
  lets you use common `adrp+add` with a `RELATIVE` slot to access a symbol within the same shared module
  - note: in assembly, you choose the type of reference, `-fno-semantic-interposition` doesn't do anything.
  - these flags affect only references in your own module, so another module may always go through 
    - GOT/PLT (ELF)
    - GOT/stub (Mach-O)
    - IAT (PE)
   We can add a note later about htese flags in detail, back to assembly and how to write GOT access now 
   that we know we need it to handle cross module access, and sometimes even same module symbol access

So, once reocation symbols for GOT have been filled
1. **Program Start (eager)**: the kernel maps the executble, for every library listed 
  as needed at startup (`DT_NEEDED` on ELF, `LC_LOAD_DYLIB` on Mach-O), *dynamic linker* (`ld.so` linux, 
  `linker64` bionic, `dyld`) processes relocations. 
  - `GLOB_DAT`: resolved before main runs 
2. **First call (lazy)**: `JUMP_SLOT` GOT entries initially point to a resolver stub. first call runs the resolver,
  which looksup the symbol and fills the entry.
3. **`dlopen` time**: Library being opened has its own GOT, and its relocations are resolved in the `dlopen`
  calls. `RTLD_NOW` resolves them all, while `RTLD_LAZY` resolved the function slots lazily. Its lookups 
  go thorugh global scome, its own dependencies, and any `RTLD_GLOBAL` libraries to resolve recursive 
  symbol references.

Cross image lookups consult the **exported symbol tables** for aach shared module.
- ELF: `.dynsym`
- Mach-O: export trie. **Two Level namespace** image (default unless `-flat_namespace`), an imported symbol 
 is idenfified by *library it came from* and *exported name*. This is Mach-O's solution to name collisions.
 this means lookups are not a global search 
- Windows: Export table of the DLL, by name or ordinal (index)
  - Windows **PE** doesn't have GOT. Uses **Import Address Table**, which records DLL Name + symbol name, and fill it 
    *before `main`*. So calls made through the slot are `blr` basically, instead of lookup by name, and doesn't use 
    any GOT specific syntax

Finally examples:

**ELF (Linux, Android)**:
```asm 
adrp x0, :got:sym              // page of sym's GOT slot
ldr  x0, [x0, :got_lo12:sym]   // x0 = *slot = real address of sym
ldr  20, [x0]                  // now access it, say a 4 bytes global
```
- contains `R_AARCH64_ADR_GOT_PAGE` and `R_AARCH64_LD64_GOT_LO12_NC` relocations after static linking.

**Mach-O (Linux, Android)**:
```asm
adrp x0, _sym@GOTPAGE
ldr  x0, [x0, _sym@GOTPAGEOFF]
```
- contains `ARM64_RELOG_GOT_LOAD_PAGE21` and `ARM_RELOC_GOT_LOAD_PAGEOFF12`. `ld64` static linker builds slots 
  in `__DATA_CONST,__got` and `dyld` fills them at launch. `ld64` can rewrite to classic `adrp + add` if symbols 
  is in the same image
  - TODO: Anki question

So between direct and GOT access, in linux/android and macos/ios

| | ELF | Mach-O |
| - | - | - |
| Page of symbol | `sym` (`ADR_PREL_PG_HI21`) | `_sym@PAGE` |
| Offset in page | `:lo12:sym` | `_sym@PAGEOFF` | 
| Page of GOT slot | `:got:sym` | `_sym@GOTPAGE` |
| Offset of GOT slot | `:got_lo12:sym` | `_sym@GOTPAGEOFF` |

**Windows on ARM64** 
- `dumpbin /exports foo.dll`: shows `foo` name, with its ordinal inside the DLL.
- `dumpbin /symbols caller.obj`: shows `__imp_foo` as an **undefined external**, used `adrp/ldr` refeences
- `dumpbin /symbols foo.lib` the **Import library** for the DLL: shows both `foo` and `__imp_foo`
- `dumpbin /imports app.exe`: shows DLL name and `foo` and the IAT address. Not `__imp_foo`
- `dumpbin /relocations caller.obj`: whoes relocations against `__imp_foo`
  - `IMAGE_REL_ARM64_PAGEBASE_REL21` for `adrp` and `IMAGE_REL_ARM64_PAGEOFFSET_12L` for `ldr`. they are 
    COFF counterparts of `ADR_PREL_PG_HI21` and `LDST64_ABS_LO12_NC`
- The import library generated by `link/lib` defines **2 symbols** (windows quirk)
  - `__imp_foo` address of the IAT slot, pointer sized cell the loader fills with the real address of `foo`
  - `foo` small thunk that jumps through `__imp_foo` pointed slot
```asm 
adrp x16, __imp_foo 
ldr  x16, [x16, :lo12:__imp_foo]
br   x16
```
- Note: undefined references are not an error in assembly

### What about Alternatives to the couple `adrp` page access + 4KB offset with `add` or `ldr`

**Literal Pool** `ldr xN, label` loads a 64-bit constant stored near code, within ±1MB from PC. Therefore,
a convenient **Pseudo Instruction**, with syntax 
```asm
// the equal sign and the enormous constants are givaway of literal pool
ldr x0, =0x123456789abcdef0
```
Creates 
```asm 
     ldr x0, 1f
     ret
   .balign 8
1: .quad 0x123456789abcdef0 ```
- why 1f? it means *forward*
- Literal pool is placed either at the end of the section or when you place a `.ltorg` directive.
  - TODO: Exercies in which literal pool is out of reach (1MB). Linux/Android experiment
- Apple's assembler doesn't support `ldr x0, =imm` pseudo instruction.

So, to reaach an address beyond ±4GB under `-fPIE`, we can store a **PC-relative difference**

```asm 
      adr  x1, 1f      // x1 = address of pool slot = slot
      ldr  x0, [x1]    // x0 = sym - slot
      add  x0, x0, x1  // x0 = sym (full 64-bit range)
      ...

   .balign 8 
1: .quad sym - 1b
```
- unless you write a massive monolith, you won't get in the situation in which a aame-image symbol needs 
  to cross a >4GB PC relatiev distance. In any case, the linker will give you an error when that happens 
  - TODO: simulate this scenario by creating a `.bss` segment with `.space` 4GB.

Related but not essential here is the concept of a **Veneer**, which is a small function stub inserted by 
the *static linker* in your shared module/executable if a `bl` invocation crosses its supports 26 bits signed 
offset in the instruction encoding, namely **±128MB**
- TODO Anki:

If `far_func` needs to travel within $GB distance, then linker switches the `bl` call with 

```asm 
__far_func_veneer:
  adrp x16, far_func              // using linux/android syntax, but you got the idea
  add  x16, x16, :lo12:far_func
  br   x16                        // note: not blr, cause we want link register to go back to our code, not veneer
```

If it goes beyond ±4GB, then it uses the constant strategy we've just talked about. If you know the function 
is distant, you also can write a Veneer by hand 
- TODO: exercies about this with `.space`?

---

### ELF AArch64, PLT

the **Procedure Linkage Table** is a set of stubs, one for each external function, that let a `bl symbol` reach a function
whose address isn't known at load time.

To let us use `bl` instead of `blr`, the *Static Linker* needs to generate a small function, which starts with `bti c` if 
`-mbranch-protection=standard`, which is pointed by `bl symbol` and performs the load of the GOT slot and `br` to it.
Since this little stub counts as an Inter Procedure Call, (IPO), the two scratch registers `x16,x17` Are used.
```
foo@plt:
   adrp x16, GOTPLT_slot_foo              // special GOT slot called .got.plt
   ldr  x17, [x16, :lo12:GOTPLT_slot_foo]
   add  x16, x16, :lo12:GOT_PLT_slot_foo  // the resolver needs address of slot to know which function we are resolving
   br   x17
```

The first time the `br x17` jumps to a **Resolver**, which will invoke the dynamic linker to lookup the desired function,
global lookup as we've explained before, then, the resolver will fill `GOTPLT_slot_foo` with the actual function address.
This means that using PLT the GOT entry for a function is a `R_AARCH64_JUMP_SLOT` until it is filled.

linking with `-z now` (BIND NOW) lets the loader fill each stub at load time. Used in Android for security cause the PLT is 
**readonly**
- TODO: Anki

By using `-fno-plt` you can remove lazy binding, and swap the `bl symbol` with a `blr` (`adrp` + `ldr` with GOT syntax)

PLT Equivalent constructs in other operating systems (Lazy binding tables):
- Mach-O: `__TEXT,__stubs`. Next section talks about this, eg `bl _puts`, where `_puts` from `System` library
- Windows PE: no PLT. The import library thunk is the lazy loading mechanism.

### Mach-O Full GOT call example

Let us now make a full exercies for an example in which we call a function cross-image by using the GOT.

1. Try to execute `cat $(xcrun --show-sdk-path)/usr/lib/libSystem.tbd` and see what symbols we export

The exmaple will bake use of `.p2align [w|l] power_of_two, fill_value, max_strip`, which is a directive which **pads** 
the section's **Location Counter** to a `power_of_two` Byte boundary. of course, we want 4-Byte aligned for instructions

```asm 
  .globl _main            // Mach-O exported symbols are _ prefixed
  .p2align 2              // 4-Byte alignment for instructions
_main:
    stp    x29, x30, [sp, #-16]!        // save fp and lr
    mov    x29, sp                      // establish frame chain

    adrp   x0, msg@PAGE                 // string in this image, no GOT
    add    x0, x0, msg@PAGEOFF
    bl     _puts                        // 1. Function called via stub, meaning __TEXT,__stubs

    adrp   x8, ___stdoutp@GOTPAGE            // 2. Fetch through GOT in libSystem. take `FILE** &stdout`
    ldr    x8, [x8, ___stdoutp@GOTPAGEOFF]   //   x8 = &stdout
    ldr    x1, [x8]                          //   x0 = stdout  (FILE*)

    // x0 = message, x1 = stdout, we can call fputs, still from stub
    bl     _fputs

    // epilogue: pop fp and lr
    ldp    x29, x30, [sp], #16
    ret
.section __TEXT,__cstring
msg: .asciz "hello world"
```

Note that while we can not use stubs in Mach-O by writing a GOT fetch and `blr` manually,
Mach-O targeting `clang` can't, as `-fno-plt` gets ignored, as it it a ELF flag.

`.section .text, "ax", %progbits` is ELF specific, meaning 
- in text segment 
- allocatable and executable 
- section holds real bytes (opposed to `%nobits` for `.bss`)
  - means allocate space at runtime but don't use disk space in executable, maps to `SHT_NOBITS` in ELF Section

**Mach-O Syntax** is Different

```asm 
   .section <segment>, <section> [, <type> [, <attributes> [, <stub-size.]]]
```

a text segment becomes 
```asm 
  .section __TEXT,__cstring,cstirng_literals
```
- `__TEXT`: **segment**: each segment has its own memory permissions
  - `__TEXT`: readonly, executable 
  - `__DATA`: read-write 
  - `__DATA_CONST`: read-only after **fixups**
    - stubbed symbols point to a `__DATA_CONST` slot, which, 
      on first execution, gets resolved with a mechanism similar to ELF's PLT, then written
      `dyld_info -fixups` executed on the binary can list the entries. Stubs themselves go 
      in a `__TEXT,__stubs` section
 - `__LINKEDIT`: don't care for now

- `__cstring`: **section**: arbitrary name at most 16 characters starting with `__`
- `cstring_literals`: **type**: usage. In particular, this holds NUL-terminated c-strings
  - linker can merge `S_CSTIRNG_LITERALS` (0x2 flag in binary) sections with identical strings

- Attributes (unused here), eg `regular`,`pure_instructions` for code.
- Stub size matters only for eections `__stubs`, meaning used for a table of pointers of 
  lazy initialized symbols

Here's the full table of clang's shortands, which I won't use to practice knowledge of 
executable formats 

| Directive | Equivalent |
| - | - |
| `.text` | `.section __TEXT,__text,regular,pure_instructions` |
| `.text` | `.section __TEXT,__cstring,cstring_literals` |
| `.const` | `.section __TEXT,__const` |
| `.data` | `.section __DATA,__data` |
| `.zerofill __DATA,__bss,sym,size` | uninitialized data. No `.bss` shortand. `size` and `sym` are placeholders |


- Note: We've said that unless `-flat_namespace` is specified on the *static linker* (library), 
  Mach-O libraries expose their symbols with a tuple (lib name, symbol name). 
  Can we specify in assembly the tuple iteself when GOT fetching a symbol or `bl` to a stub?
  **No**.

```shell
# liba and libb export function _foo
clang -nostdlib -lSystem m.S liba.dylib libb.dylib -o ab   # imports: _foo (from liba)
clang -nostdlib -lSystem m.S libb.dylib liba.dylib -o ab   # imports: _foo (from libb)
```
So, to control what your code actually calls, you either 
- check the order of imported libraries, put what you want first
- Link with an alias to disambiguate the names `-Wl,-alias,_orig,_alias`
  - linker level rename
- fetch the function at runtime with `dlopen` and `dlsym`
  - TODO Anki: BTW, `dlopen` and `dlsym` **Don't use GOT and PLT/stubs** (ELF/Mach-O).

### Stuff copied from claude code about dlopen and GOTs 

What each call does

dlopen(path, flags)
1. Finds and maps the library, and recursively loads its dependencies (DT_NEEDED on ELF, LC_LOAD_DYLIB on Mach-O).
2. Processes that library's own relocations: its GOT slots (GLOB_DAT, RELATIVE), its rebase fixups, and its PLT slots. This is exactly the work done at program start for the executable and its startup libraries. RTLD_NOW resolves everything now. RTLD_LAZY leaves function slots for first-call binding on ELF, and Mach-O mostly binds eagerly regardless.
3. Runs its initializers (DT_INIT_ARRAY / __mod_init_func) and returns a handle.

So the GOT/PLT of the new library gets filled during dlopen. Your executable's GOT is not touched. Nothing you linked against is rewritten to point into the new library.

dlsym(handle, "name")
- It searches an exported symbol table and returns the address: ELF .dynsym, found through the hash table (DT_GNU_HASH/DT_HASH), or Mach-O's export trie. No GOT slot is read or written.
- You store the result in a function pointer and call it with blr x. That's a direct indirect call: no PLT and no stub.
- Mach-O's dlsym takes the name without the leading underscore ("foo", not "_foo"), and dyld adds it.

Search scope

|   Argument   |                                     ELF                                          |             Mach-O              |
| ------------ | -------------------------------------------------------------------------------- | ------------------------------- |
| a handle     | that library and its dependencies                                                | that dylib and what it re-exports         |
| RTLD_DEFAULT | the global scope in load order (exe, LD_PRELOAD, startup libs, RTLD_GLOBAL libs) | all loaded images               |
| RTLD_NEXT    | the next object after the caller in search order                                 | the next image after the caller |

Note that dlsym with RTLD_DEFAULT on Mach-O does a name search across images. The two-level binding applies to link-time imports, not to this call.

What is shared and what isn't

┌─────────────────────────────────────────┬────────────────────────────────────────────────────────────────┐
│                                         │                          Lookup code                           │
├─────────────────────────────────────────┼────────────────────────────────────────────────────────────────┤
│ Startup binding of GLOB_DAT/bind fixups │ the dynamic linker's relocation processing, shared with dlopen │
├─────────────────────────────────────────┼────────────────────────────────────────────────────────────────┤
│ ELF lazy PLT resolver                   │ same lookup routine as dlsym                                   │
├─────────────────────────────────────────┼────────────────────────────────────────────────────────────────┤
│ dlsym                                   │ same export-table search, driven by your arguments             │
└─────────────────────────────────────────┴────────────────────────────────────────────────────────────────┘

So a lazy PLT resolution and a dlsym are the same operation at heart: find a name in exported symbols. The difference is who gets the result. The resolver writes it into a GOT slot, dlsym hands it to you.

Consequences

- Already-bound slots stay. If your executable's slot for foo pointed at library A, loading library B with another foo doesn't change it. Only later-loaded libraries can bind to RTLD_GLOBAL symbols.
- dlclose and dangling slots: other modules' slots that were bound into a library you unload become dangling, so the loader typically keeps a library alive while anything depends on it.
- Android: dlopen also obeys linker namespaces, so which libraries are visible depends on the caller's namespace.
