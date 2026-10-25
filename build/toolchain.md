# The pinned toolchain

This file owns the versions every build, check and release of pdfkit is made
with, as the commands printed them on the owner's laptop (Windows 11 Enterprise
10.0.26100, x64, no administrator rights) on 2026-10-24. A version moves only by
a change to this file and a full run of `python build/check.py`, because a
compiler or formatter that changes under the code changes its warnings, its
formatting and its output.

## 1. Versions

| Tool | Command | What it printed |
|---|---|---|
| GCC (C and C++) | `g++ --version` | `g++.exe (MinGW-W64 x86_64-ucrt-posix-seh, built by Brecht Sanders, r2) 16.1.0` |
| CMake | `cmake --version` | `cmake version 4.3.2` |
| Ninja | `ninja --version` | `1.13.2` |
| binutils | `objdump --version` | `GNU objdump (Binutils for MinGW-W64 x86_64, built by Brecht Sanders, r2) 2.46.0.20260210` |
| Python | `python --version` | `Python 3.12.10` |
| .NET SDK | `dotnet --list-sdks` | `10.0.204 [C:\Program Files\dotnet\sdk]` |
| clang-format | `clang-format --version` | `clang-format version 23.1.3` |
| clang-tidy | `clang-tidy --version` | `LLVM version 23.1.3` |
| git | `git --version` | `git version 2.53.0.windows.1` |
| curl (Git for Windows) | `curl --version` | `curl 8.18.0 (x86_64-w64-mingw32) libcurl/8.18.0 Schannel` |
| NASM | `nasm -v` | not installed: libjpeg-turbo is built with its SIMD extensions off (`third_party/libjpeg-turbo/CMakeLists.txt` leaves `WITH_SIMD` unset), so DCT decoding runs its portable C code |

GCC, CMake, Ninja and binutils come from one WinLibs package (UCRT runtime,
POSIX threads, SEH exceptions), installed through winget for the user, in:

```
C:\Users\oliber001\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin
```

That folder is on `PATH`; the presets name the compilers `gcc` and `g++` and
find them there.

## 2. clang-format and clang-tidy

Both come from PyPI as wheels that bundle the LLVM binaries, pinned by version
and by the SHA-256 of the win_amd64 wheel in `build/requirements-dev.txt`, and
installed per user:

```
python -m pip install --user --require-hashes -r build/requirements-dev.txt
```

| Package | Version | Wheel | SHA-256 | Licence |
|---|---|---|---|---|
| `clang-format` | 23.1.3 | `clang_format-23.1.3-py2.py3-none-win_amd64.whl` | `6c2b74c3a3e942ec172d768e7b945f8e136db05a7b735d49ecfc69bda97643ad` | Apache-2.0 (wheel metadata); the LLVM binaries are Apache-2.0 WITH LLVM-exception |
| `clang-tidy` | 23.1.3 | `clang_tidy-23.1.3-py2.py3-none-win_amd64.whl` | `cf0d0e671f707b5a668c035382b964056067076445e87b736b19ff482229010a` | as above |

The wheels are built by the ssciwr projects (https://github.com/ssciwr/clang-format-wheel,
https://github.com/ssciwr/clang-tidy-wheel), read from the wheel metadata. The
executables land in the user scripts folder
`C:\Users\oliber001\AppData\Roaming\Python\Python312\Scripts`, which is not on
`PATH`; `build/check_format.py` finds them there through Python's `sysconfig`
(`nt_user` scheme), or on `PATH`.

clang-tidy reads `out/mingw-debug/compile_commands.json`, whose compiler is
GCC. It is run with `--extra-arg=--target=x86_64-w64-mingw32` so clang uses the
MinGW headers of the GCC it finds on `PATH` (measured: a file including
`<string>`, `<span>` and `<charconv>` analyses cleanly that way), and with
`--extra-arg=-Wno-unknown-warning-option`, because the pinned warning set has
GCC-only options clang does not know.

## 3. Linking without a compiler runtime

Measured by building a ten-line DLL that throws, catches, locks a
`std::mutex` and formats a string, then reading its import table with
`objdump -p <dll> | grep "DLL Name"`:

| Link flags | DLLs imported |
|---|---|
| none | `libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`, `KERNEL32.dll`, five `api-ms-win-crt-*.dll` |
| `-static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive` | `KERNEL32.dll` and `api-ms-win-crt-*.dll` only |
| `-static` | `KERNEL32.dll` and `api-ms-win-crt-*.dll` only |

The build uses `-static` on the link line of `pdfkit.dll`, `pdfkit.exe` and the
test executable (`build/runtime.cmake`), the simplest of the two that work.
The `api-ms-win-crt-*.dll` sets are the Universal C Runtime, part of Windows 10
and later, so the DLL ships alone. `build/check_artefact.py` holds the allowed
list.

## 4. Sanitizers

Measured by compiling and running a ten-line program per sanitizer (a signed
overflow for the undefined-behaviour checks, a heap write one past an array
for the address checks):

| Compiler and flags | Result |
|---|---|
| GCC `-fsanitize=undefined -fsanitize-trap=all` | builds; the overflow stops the program with an illegal instruction (exit 132): **works** |
| GCC `-fsanitize=undefined` | `cannot find -lubsan`: no runtime, **unsupported** |
| GCC `-fsanitize=address` | `cannot find -lasan`: **unsupported** |
| GCC `-fsanitize=thread` | `cannot find -ltsan`: **unsupported** |
| GCC `-fsanitize=leak` | `cannot find -llsan`: **unsupported** |
| llvm-mingw clang `-fsanitize=address` | reports `heap-buffer-overflow` with the source line: **works** |
| llvm-mingw clang `-fsanitize=undefined` | reports `signed integer overflow` with the source line: **works** |
| llvm-mingw clang `-fsanitize=address,undefined` | **works** |

So the sanitizer tier is two runs: the `sanitize` test preset (GCC, UBSan in
trap mode, `mingw-sanitize`) and the `clang-sanitize` test preset (llvm-mingw,
ASan and UBSan with their runtimes). The second is the owner's question in
`OPEN-QUESTIONS.md`; it runs while that question stands, with the recommended
answer taken.

### The sanitizer toolchain

| Field | Value |
|---|---|
| Release | llvm-mingw 20261020, `llvm-mingw-20261020-ucrt-x86_64.zip` (clang 23.1.4) |
| Upstream | https://github.com/mstorsjo/llvm-mingw/releases/tag/20261020 |
| SHA-256 | `fd5462b7f5d93b2ff981fcd3f9c7e427e47f3203c64ea91c27bcc444eebd74cd` (equal to the digest the release lists) |
| Licence | Apache-2.0 WITH LLVM-exception (LLVM), mingw-w64's own permissive licences; a test-only tool, never shipped |
| Location | `build/cache/llvm-mingw/llvm-mingw-20261020-ucrt-x86_64/` of the main checkout, found by `build/llvm-mingw.cmake` through `git rev-parse --git-common-dir`, or `PDFKIT_LLVM_MINGW` |

Fetch it, from the main checkout (the zip is about 190 MB):

```
mkdir -p build/cache/llvm-mingw
curl --ssl-no-revoke -L -o build/cache/llvm-mingw/llvm-mingw-20261020-ucrt-x86_64.zip https://github.com/mstorsjo/llvm-mingw/releases/download/20261020/llvm-mingw-20261020-ucrt-x86_64.zip
sha256sum build/cache/llvm-mingw/llvm-mingw-20261020-ucrt-x86_64.zip
python -I -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" build/cache/llvm-mingw/llvm-mingw-20261020-ucrt-x86_64.zip build/cache/llvm-mingw
```

GCC stays the pinned compiler of every artefact; llvm-mingw builds the
sanitizer run of the unit tier only.

## 5. The proxy

The laptop reaches the internet through a corporate proxy that inspects TLS.
No proxy variable is set in the environment and `pip` has no configuration;
each tool's behaviour, measured on 2026-10-24:

| Command | Outcome |
|---|---|
| `python -m pip index versions clang-format`, `python -m pip download clang-format==23.1.3` | reach PyPI and download |
| `python -c "import urllib.request; urllib.request.urlopen('https://github.com/')"` | status 200 |
| `git ls-remote https://github.com/doctest/doctest.git` | answers |
| `git ls-remote git@github.com:doctest/doctest.git`, `ssh -T git@github.com` | answer; SSH authenticates as the owner's GitHub account |
| `curl https://github.com/` (Git for Windows curl, Schannel) | fails: `CRYPT_E_NO_REVOCATION_CHECK (0x80092012)`, the revocation server is unreachable |
| `C:\Windows\System32\curl.exe https://github.com/` | fails the same way |
| `curl --ssl-no-revoke https://github.com/...` | works (status 200, downloads follow redirects with `-L`) |

So: Python's `urllib` and `pip`, and `git` over HTTPS and SSH, work as they are;
`curl` needs `--ssl-no-revoke`. A tool that downloads uses Python's `urllib` or
`git`, never `curl` without that flag.
