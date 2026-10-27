# The pinned toolchain

This file owns the versions every build, check and release of pdfkit is made
with, as the commands printed them on the owner's laptop (Windows 11 Enterprise
10.0.26100, x64, no administrator rights) on 2026-10-24. A version moves only by
a change to this file and a full run of `python build/check.py --full`, because a
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
| ccache (in the WinLibs package) | `ccache --version` | `ccache version 4.13.6` |
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
sanitizer run of the unit tier and the sanitizer dataset pass (§6.5), and its
`ld.lld` links the debug presets (§6.3). Its release is named in
`build/llvm-mingw-release.cmake`.

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

## 6. Build speed

The laptop's endpoint agents (application control and Defender) make each
process cost about 0.25 s to start and the first read of a file just written
about 10 ms; ninja starts its jobs from one thread, one every 0.4 s under
load. So a build is fast when it starts few processes and compiles each source
once, whichever worktree it is in. Every setting below was measured on the
laptop (wall time, eight logical processors, the machine otherwise idle unless
stated); a setting that did not pay is listed with the number that rejected
it, so it is not tried again blind.

### 6.1 The compile cache

| Field | Value |
|---|---|
| Tool | ccache 4.13.6, shipped in the pinned WinLibs `mingw64\bin` (`ccache --version`) |
| Licence | GPL-3.0-or-later; a developer tool, never linked or shipped (`OPEN-QUESTIONS.md` §2.16) |
| Wiring | `build/compiler-cache.cmake`: `CMAKE_<LANG>_COMPILER_LAUNCHER` is `ccache` with its settings as `KEY=VALUE` arguments, for every preset, GCC and clang alike |
| Cache | `build/cache/ccache/` of the main checkout, 20 GB at most |
| Switch | `-DPDFKIT_COMPILER_CACHE=OFF` (or ccache absent from `PATH`) builds without it; `ON` fails when it is absent; `AUTO`, the presets' value, uses it when found |
| Statistics | `ccache -d <main checkout>/build/cache/ccache -s` |

The settings and why: `base_dir` is the source tree, so a worktree's absolute
paths are rewritten relative to the build folder and two worktrees hash the
same command; `hash_dir=false` shares debug objects across worktrees too (their
debug information names the build folder that compiled them first; file and
line stay right); `depend_mode` reads the compiler's `-MD` output instead of
running the preprocessor a second time. The release DLL and EXE are
byte-identical with the cache on and off (`d7da763c...` for the DLL of
`main`). `python build/package.py --verify` sets `CCACHE_DISABLE=1` for its
rebuild, so the reproducibility it proves is the compiler's, not the cache's.

sccache (Apache-2.0, v0.18.0) was tried first. It caches GCC and clang objects
(a hit 0.58 s), but reads its settings, the cache folder and the base
directories alike, only in its server process, from that process's
environment: a client's `SCCACHE_BASEDIRS` gave 0 hits across two trees, the
server's 1. Ninja passes no per-rule environment, so a cache shared by every
worktree would need a wrapper process per compile or a server restarted under
the builds already using it. A cross-tree hit cost 1.97 s under sccache (it
preprocessed again) and 0.55 s under ccache.

| Release preset, library, CLI and unit tests (414 steps) | Wall |
|---|---|
| no cache | 429 s (457 s at the session's start, other load) |
| cold cache (every compile a miss) | 537 s: a miss costs about 2.2 s more than the compiler alone, hashing every header it read |
| a second worktree, cache warm (405 of 405 compiles hits) | 215 s: one process per step, so ninja's start rate bounds it |

### 6.2 The PDFium comparisons

An input file never changes under its SHA-256 and PDFium never changes under
its pinned release, so what PDFium says of an input is computed once and kept
in `build/cache/pdfium-results/<release>/` of the main checkout
(`tests/pdfium/results_cache.py`; about 210 MB for the 960 files): its text,
Info fields and page count, the size and SHA-256 of each page rendered at
100 dpi, its image objects and their encoded bytes, and the census's
comparison of each image (keyed by PDFium's object and pdfkit's PNG). Only the
outputs a run writes are asked of PDFium each time; an output page whose
pixels hash equal to its cached input page is identical without rendering the
input again. Every PDFium request runs in a library just reset
(`pdfium.Library.reset`), because PDFium carries font state from one document
to the next (`OPEN-QUESTIONS.md` §2.17); a cached answer is the file's own.
The treated images are judged inside the PDFium workers, in memory, so no
bitmap is written unless it is a survival's evidence (the old tooling wrote
2.3 GB per run), and the leak check decodes outputs in processes. Every tool's
`--workers` defaults to half the logical processors. Delete the folder to start
again; the next run is the cold one.

Run `bs-old` (960 files, release build of `main`), four workers, seconds:

| Step | Old tooling | New, cold cache | New, warm cache |
|---|---|---|---|
| `text_agreement.py --hidden` | 26 (its own text cache warm) | 46 | 26 |
| `image_agreement.py` (census) | 449 | 506 | 263 |
| `image_agreement.py --listing` | 235 | 200 | 195 |
| `compare.py --check write,select,text,redact` | 266 | 284 | 209 |
| `images.py` | 90 | 48 | 27 |
| `leak.py --metadata` | 56 | 37 | 30 |
| `leak.py --images` | 70 | 46 | 26 |
| `metadata.py` | 202 | 164 | 156 |
| gates 7 to 10 | 23 | 16 | 11 |
| total | 1,417 | 1,347 | 943 |

What is left is pdfkit's own side: the census tool, `pdfkit images` and
`pdfkit metadata` run once per file, a process each, and every output page is
rendered. A whole gate run of a new build (`bs-final`: `tests/dataset/run.py`
twice over the 960 files, 528 s, then the steps above, 870 s with the cache
warm) takes 1,398 s; the same run with the old tooling took 2,089 s (`bs-old`:
672 s and 1,417 s). The tables of the cold and the two warm runs are identical; against
the old tooling every table is identical but the two rows that read
order-dependent pages (§2.17).

### 6.3 Compile and link settings

Measured with the compile cache off, clean builds of 414 steps unless stated:

| Setting | Where | Measured | Kept |
|---|---|---|---|
| line tables only (`-g1`, `CMAKE_<LANG>_FLAGS_DEBUG`) | mingw-debug | clean 405 s → 376 to 381 s; `libpdfkit_core.a` 273 → 164 MB, `pdfkit_tests.exe` 255 → 86 MB, `pdfkit.dll` 155 → 54 MB; gdb gives file and line on every frame, not the arguments and locals | yes |
| lld (`PDFKIT_LINKER=lld`, `build/linker.cmake`) | mingw-debug, mingw-sanitize | clean 376 → 361 s; a one-file rebuild 13.1 → 9.2 s (three runs each); the import tables unchanged, the unit tier passes | yes |
| `-O1` (`CMAKE_<LANG>_FLAGS_DEBUG=-g -O1`) | clang-sanitize | clean 284 → 306 s; the unit tier 16.0 → 3.7 s; the dataset pass of §6.5 | yes |
| a precompiled header of standard-library headers | pdfkit_core, pdfkit_tests | release 399 → 396 s, debug 405 → 402 s: within the noise | no |
| unity builds, batches of 8 and 16 | release | 44 sources clash on file-local names (`shape`, `refuse`, `max_depth`, `key`...), the set grew each time an opt-out moved the batches, and -O3 inlining across files raised new `-Wnull-dereference` errors in `document.cpp`, `parser.cpp`, `self_check.cpp` and `redact_images.cpp`: the shipped DLL would be other code | no |
| lld for the release DLL | mingw-release | not adopted: the shipped DLL keeps GNU ld, whose bytes `package.py --verify` reproduces | no |
| splitting `src/redact/text_redactor.cpp` (the slowest source, 27 to 48 s) | — | its 650 lines of file-local helpers serve the two methods of one class; there is no second class to give a file | no |

Each source compiles alone in every preset, so a source names every header it
uses and its file-local names stay its own; nothing needs an opt-out. To debug
with locals, configure mingw-debug with `-DCMAKE_CXX_FLAGS_DEBUG=-g`
`-DCMAKE_C_FLAGS_DEBUG=-g`; to link it with GNU ld, `-DPDFKIT_LINKER=default`.
lld is found in the cached llvm-mingw (§4); without it the debug presets link
with GNU ld and say so. GCC finds `ld.lld` through `-B<llvm-mingw>/bin/`, which
also puts that folder on the library search path; it holds DLLs and no import
library of a name the link asks for, which the import tables confirm.

Ninja starts one job every 0.4 s from one thread under load, so a build of
mostly cache hits is bounded by its step count, not by the compiler: a warm
worktree's release build (414 steps) takes 215 s whatever `-j` says (`-j8`:
218 s).

When adding a file: nothing changes; it is cached like the others. When adding
a target: it inherits the launcher and, in the debug presets, the linker.

### 6.4 The checks

`python build/check.py --full` is the merge gate (CLAUDE.md §8.1 and §10);
`python build/check.py` the routine run; `--fast` the pre-push hook's.
clang-tidy keeps each source's findings in `build/cache/clang-tidy/` of the
main checkout, under the SHA-256 of the tool's version, `.clang-tidy`, the
arguments, the compile command (the tree's paths written as `<root>`) and the
content of every project file the source reaches through `#include`
(`build/check_format.py`, `IncludeGraph`: every include line counts, whatever
conditional surrounds it, so the set is a superset of what the compiler
reads). A routine or fast run analyses only the sources that reach a file
changed since `--base` (default the remote main branch); a change to
`.clang-tidy`, the check scripts, the pinned versions or any CMake file selects
every source. Measured (formatting alone takes 15 s of each):

| clang-format and clang-tidy | Wall |
|---|---|
| every source, cache set aside (`--full`) | 271 s |
| every source, all from the cache | 20 s |
| one source edited | 23 s |
| `src/platform/error.hpp` edited (84 sources reach it) | 105 s |
| nothing changed | 19 s |

Whole runs on this branch (whose CMake changes select every source):
`--full` 1,034 s (mingw-debug built with the compile cache cold 489 s,
clang-tidy of everything 276 s, mingw-release from a warm cache 202 s); the
routine run straight after it 38 s and `--fast` 32 s, every source's findings
from the cache. Before, `python build/check.py` was the full run with neither
cache: about 1,100 to 1,200 s from its measured parts (mingw-debug 405 s, clang-tidy
271 s, mingw-release 399 to 457 s).

### 6.5 The sanitizer dataset pass

At a phase's end the `clang-sanitize` build reads every manifest file:

```
cmake --preset clang-sanitize && cmake --build --preset clang-sanitize
python tests/dataset/run.py --sanitizer --runs 1 --timeout-scale 5 --run-id <run-id>-asan
```

`--sanitizer` runs `out/clang-sanitize/pdfkit.exe` with the sanitizer
runtime's folders (from the build's CMake cache) on `PATH`, searches every
run's standard error for an AddressSanitizer or UndefinedBehaviorSanitizer
report (UBSan prints and goes on, so the exit code alone does not tell) and
fails the run on one. The preset compiles at `-O1` with
`-fno-omit-frame-pointer`, AddressSanitizer's recommendation; at `-O0` the
pass took 2,263 s and 157 of the 960 files ran out of time (`p10-asan`). At
`-O1`, four workers: 766 s, 935 files within the release limits, the other 25
finishing at up to 3.3 times theirs (`dccc-0129`, 198 s for 394 pages), no
sanitizer report and no crash; `--timeout-scale 5` lets every file finish with
room for a loaded machine. Run as documented (`bs-final-asan`): all 960 files
in 1,039 s, every expectation held, no sanitizer report, the slowest at 3.6
times its release limit.
