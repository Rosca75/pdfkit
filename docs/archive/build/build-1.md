# build-1 — Toolchain spike, repository skeleton, C ABI hello and CLI skeleton

| Field | Value |
|---|---|
| Phase | 1 |
| Branch | `phase/1-skeleton` |
| Depends on | nothing |
| Parallel with | nothing |
| Model | `opus` |
| Owns | `CMakeLists.txt`, `CMakePresets.json`, `.clang-format`, `.clang-tidy`, `.gitignore`, `.github/`, `include/pdfkit/`, `src/`, `cli/`, `tests/unit/`, `tests/abi/`, `third_party/doctest/`, `third_party/inventory.tsv`, `build/`, `docs/ABI.md`, `OPEN-QUESTIONS.md` |
| Size | about 2,500 lines, tests and scripts included (most of it check scripts, stubs and CMake) |
| Price | estimate: 5 to 8 hours, $70 to $130 of model spend at list prices, calibrated from change-12's batch A (about 3,200 lines of C#, 8 to 10 h, $120 to $180) scaled to a smaller, mostly mechanical phase |

**Phase graph.** 1 → {2, 3}; 3 → {4, 6, 7}; 4 → 5; 5 → 6's listing step;
{5, 7} → 8; {6, 7, 8} → 9; every phase → 10. Phase 2 runs in parallel with 3;
4, 6 and 7 run in parallel. This phase is the root: nothing starts before it
merges, so it ends with every folder, every catalogue symbol and every check
script in place, and later phases add files rather than reshape shared ones.

---

## 0. What this phase ships

No catalogue function works yet. What ships is the frame every later phase fills:

- **The toolchain pin**, `build/toolchain.md`, written from what the commands
  print on the laptop, not from this file.
- **The C ABI** (`include/pdfkit/pdfkit.h`) declaring every symbol of
  `CLAUDE.md` §2 and the five lifecycle symbols, each implemented in
  `src/api/<function>.cpp`. `pdfkit_abi_version` and `pdfkit_version` work; every
  catalogue function returns `PDFKIT_E_NOT_IMPLEMENTED` with an actionable JSON
  error naming the function and the build file that delivers it. Declaring the
  whole surface now means later phases never edit the header except to add a
  row for a function the catalogue gains, which is what keeps parallel phases
  from colliding on it.
- **The catalogue registry**, `src/api/catalogue.cpp`, one row per catalogue
  function (number, C symbol, CLI verb, status), and `api::guarded`, the
  catch-all every exported body goes through (`CLAUDE.md` §3.3).
- **The CLI skeleton**: `cli/main.cpp` and one file per verb under `cli/verbs/`
  for every verb of `CLAUDE.md` §2, wired to the C ABI. An unimplemented verb
  exits 4 with the JSON error the ABI returned.
- **The platform layer basics** (§2 below): the only place for OS and compiler
  specifics, plus the byte, error, budget, checked-arithmetic and JSON types
  every layer uses.
- **Every layer folder** of `CLAUDE.md` §3.1 with its `CMakeLists.txt` and one
  placeholder source holding the purpose header, so the layer targets exist and
  `build/check_layout.py` has something to read.
- **Every check script of `CLAUDE.md` §8.1 and §9**, the pre-push hook, the CI
  workflow, the packaging skeleton and the size budget file.
- **The ABI tier**: a .NET 10 console project and a Python `ctypes` one-liner
  that load `pdfkit.dll` and print the ABI version.
- **`docs/ABI.md`**: the symbols, the status codes, the buffer rule, the JSON
  error object, the versioning rule, and the exit-code mapping of the CLI.

Catalogue rows delivered: none as functions. CLI verbs delivered: all of them as
wired stubs.

## 1. What it reads from earlier phases

Nothing: this phase is the first. Read `CLAUDE.md` whole (chapters 1, 2, 3, 6,
8, 9, 10 and 11 are the specification of this phase) and the repository as it
stands (`LICENSE`, `NOTICE`, `README.md`, `THIRD-PARTY-NOTICES.md`,
`OPEN-QUESTIONS.md`, `CLAUDE.md`, `docs/DESIGN.md`, `docs/SOURCES.md`, the ten
build files). `docs/DESIGN.md` chapter 19 holds the refusal and warning codes;
the status codes of `docs/ABI.md` must be able to carry them.

## 2. Components

### 2.1 The toolchain spike (first, one sitting)

| File | What it holds | Lines | Reference and status |
|---|---|---|---|
| `build/toolchain.md` | the versions printed by `g++ --version`, `cmake --version`, `ninja --version`, `python --version`, `dotnet --list-sdks`, `objdump --version`, and the clang-format and clang-tidy versions installed from PyPI; the full path of the WinLibs `mingw64\bin` folder; whether `pip`, `git` over HTTPS and SSH, and `curl` pass the proxy (each command and its outcome); which sanitizers this GCC supports on Windows, measured by compiling and running a ten-line program per sanitizer; the static-runtime link flags that worked | 120 | measured on the laptop. Already observed by the team lead before this phase and to be re-measured, not copied: GCC 16.1.0 (MinGW-W64 x86_64-ucrt-posix-seh, built by Brecht Sanders, r2), CMake 4.3.2, Ninja 1.13.2, Python 3.12.10, .NET SDK 10.0.204; `pip download` reaches PyPI; `git ls-remote` over SSH reaches GitHub; no administrator rights |
| `build/requirements-dev.txt` | `clang-format` and `clang-tidy` wheels pinned by version and `--hash`, installed with `python -m pip install --user --require-hashes -r build/requirements-dev.txt` | 10 | the PyPI packages `clang-format` and `clang-tidy` exist as wheels bundling the LLVM binaries (recalled; verify on pypi.org and record the version and licence, Apache-2.0 with LLVM exception, in `build/toolchain.md`) |

The expectations to measure, each recorded as found:

- UBSan in trap mode (`-fsanitize=undefined -fsanitize-trap=all`) is expected to
  work under MinGW-w64 because trap mode needs no runtime library (recalled).
- AddressSanitizer is expected to be unsupported by MinGW GCC on Windows
  (recalled: libasan is not built for the MinGW targets). If it fails, record
  the error and write the owner question of §5.2 into `OPEN-QUESTIONS.md`.
- A shared library linked with `-static-libgcc -static-libstdc++` and
  `-Wl,-Bstatic -lwinpthread` (or `-static` on the DLL link line) is expected to
  import only `KERNEL32.dll`, `msvcrt`-free UCRT API sets
  (`api-ms-win-crt-*.dll`) and other system DLLs (recalled). Measure with
  `objdump -p pdfkit.dll | findstr "DLL Name"` and record the list.

### 2.2 Build system

| File | What it holds | Lines | Reference and status |
|---|---|---|---|
| `CMakeLists.txt` | project `pdfkit` (C and CXX), `CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`, the version, the subdirectories, the targets of `CLAUDE.md` §10 (`pdfkit_core` static, `pdfkit` shared, `pdfkit_cli` producing `pdfkit.exe`, `pdfkit_tests`), the static runtime on the shared library, `PDFKIT_BUILDING` defined for the export macro | 120 | CMake documentation for `CMAKE_CXX_EXTENSIONS` (recalled) |
| `CMakePresets.json` | `mingw-debug`, `mingw-release`, `mingw-sanitize` configure, build and test presets with the Ninja generator, binary dirs under `out/<preset>/`; test presets `unit` and `sanitize` (ctest label `unit`) | 90 | CMake presets schema version 6 or later (recalled; use the newest version CMake 4.3.2 accepts) |
| `build/warnings.cmake` | the pinned GCC warning set applied to every target of ours: `-Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast -Wcast-align -Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough -Wmisleading-indentation -Wduplicated-cond -Wlogical-op`; a function `pdfkit_third_party_target(t)` that applies `-w` to vendored C targets only, each call naming the component; the named-suppression list (empty) with the rule that each entry carries its file, line and reason | 60 | GCC manual warning options (recalled) |
| `src/<layer>/CMakeLists.txt` | one per layer of `CLAUDE.md` §3.1 (`platform`, `syntax`, `fonts`, `text`, `images`, `writer`, `redact`, `api`), each adding its sources to `pdfkit_core` with `target_sources`, so a phase adds a file by editing only its own layer's list | 8 × 15 | |
| `src/<layer>/<layer>_placeholder.cpp` | for the layers with no code yet (`syntax`, `fonts`, `text`, `images`, `writer`, `redact`): licence header, purpose header saying what the layer will hold (one sentence from `CLAUDE.md` §3.1), and an empty `namespace pdfkit::<layer> {}`; the phase that adds the layer's first real file deletes it | 6 × 10 | |
| `.clang-format` | based on `Google` or `LLVM`, column limit 100, the include order (own header, `pdfkit/`, layer headers, third party, standard library) | 30 | |
| `.clang-tidy` | `bugprone-*`, `cert-*` (minus the checks that duplicate warnings), `cppcoreguidelines-owning-memory`, `cppcoreguidelines-no-malloc`, `modernize-*` (minus `use-trailing-return-type`), `performance-*`, `readability-identifier-naming` with the naming rules of `CLAUDE.md` §3.5 (types `CamelCase`, functions and variables `snake_case`, members `snake_case_`), `WarningsAsErrors: '*'` | 40 | |
| `.gitignore` | `out/`, `dist/`, `dataset/files/`, `dataset/cache/`, `dataset/reports/`, `build/cache/`, `tests/abi/dotnet/bin/`, `tests/abi/dotnet/obj/`, `__pycache__/` | 15 | |

### 2.3 The platform layer (`src/platform/`, namespace `pdfkit::platform`)

| File | What it holds | Lines |
|---|---|---|
| `export.hpp` | `PDFKIT_API` (`__declspec(dllexport)` when building, nothing for the static core, the visibility attribute elsewhere); the only `#ifdef _WIN32` the tree may hold outside this folder is none | 30 |
| `byte_span.hpp` | `ByteSpan` (a `std::span<const std::byte>` alias with helpers to read big- and little-endian integers bounds-checked) | 80 |
| `buffer.hpp`, `buffer.cpp` | `Buffer`, an owned growable byte vector whose growth is charged to a `Budget` | 120 |
| `error.hpp`, `error.cpp` | `Error`, the one exception type of the core: a code (from the list of `docs/DESIGN.md` chapter 19 plus `internal`, `not_implemented`, `invalid_argument`), and the three message parts `failed`, `expected`, `fix`, each required non-empty by the constructor so an unactionable error cannot be built | 90 |
| `checked.hpp` | checked add, multiply and narrowing conversions that throw `Error` (`limit_exceeded`) on overflow, used for every value read from a file | 80 |
| `budget.hpp`, `budget.cpp` | `Budget`: per-stream and per-document byte limits (512 MiB and 2 GiB by default, configurable), an object-count limit, `charge(bytes)` throwing `limit_exceeded` with the object, the limit and the way out | 110 |
| `json_writer.hpp`, `json_writer.cpp` | a streaming JSON writer: objects keep insertion order, strings escaped per RFC 8259, numbers through `std::to_chars` (shortest round-trip for doubles, fixed for rounded geometry), so output is byte-deterministic | 220 |
| `json_reader.hpp`, `json_reader.cpp` | a strict JSON reader into a small value tree, with a depth limit of 64 and a size limit, and errors that name the byte offset, what was expected and how to fix the input | 320 |

### 2.4 The C ABI (`include/pdfkit/`, `src/api/`, namespace `pdfkit::api`)

| File | What it holds | Lines |
|---|---|---|
| `include/pdfkit/pdfkit.h` | C header, `extern "C"` guarded, no C++ in it: `pdfkit_status` enum (`PDFKIT_OK = 0`, `PDFKIT_E_INVALID_ARGUMENT`, `PDFKIT_E_REFUSED`, `PDFKIT_E_SELF_CHECK`, `PDFKIT_E_NOT_IMPLEMENTED`, `PDFKIT_E_INTERNAL`), `pdfkit_buffer` (`const uint8_t* data; size_t size; void* reserved`), opaque `pdfkit_document` and `pdfkit_edit`; every symbol of `CLAUDE.md` §2 with the shape `pdfkit_status pdfkit_<fn>(<handle or bytes>, const char* options_json, pdfkit_buffer* out)` (`pdfkit_open` and `pdfkit_check` take `const uint8_t* bytes, size_t size`; `pdfkit_open` returns the handle through `pdfkit_document** out_doc` and its report through `out`; `pdfkit_image_data` takes the identity as `const char*`); the lifecycle symbols `pdfkit_abi_version(void)` returning `uint32_t`, `pdfkit_close`, `pdfkit_edit_new`, `pdfkit_edit_free`, `pdfkit_buffer_free`; plus `pdfkit_version(pdfkit_buffer* out)` returning the JSON `{"version": …, "abi": …, "build": …}`. A doc comment per symbol names its catalogue row | 220 |
| `src/api/guarded.hpp`, `guarded.cpp` | `api::guarded(out, fn)`: runs `fn`, maps `platform::Error` to its status and JSON error object (`code`, `failed`, `expected`, `fix`), maps `std::bad_alloc` and every other exception to `PDFKIT_E_INTERNAL` with a message that tells the caller to report the input, and never lets an exception escape | 120 |
| `src/api/handles.hpp`, `handles.cpp` | the opaque handle structs (`pdfkit_document` holds nothing yet but its options; `pdfkit_edit` holds a pointer to its document), creation and destruction, null checks returning `PDFKIT_E_INVALID_ARGUMENT` with a message | 120 |
| `src/api/catalogue.hpp`, `catalogue.cpp` | `CatalogueRow { std::string_view number, symbol, verb, build_file; Status status; }` and the constant table, one row per §2 row, read by the not-implemented error and by `build/check_catalogue.py` (which parses this file) | 90 |
| `src/api/<function>.cpp` | one per catalogue function: `open.cpp`, `inspect.cpp`, `text.cpp`, `images.cpp`, `image_data.cpp`, `metadata.cpp`, `redact_text.cpp`, `redact_images.cpp`, `set_metadata.cpp`, `channels.cpp`, `select_pages.cpp`, `write.cpp`, `check.cpp`; each argument-checks, then returns `not_implemented` with the build file from the registry | 13 × 30 |
| `src/api/lifecycle.cpp` | `pdfkit_abi_version`, `pdfkit_version`, `pdfkit_close`, `pdfkit_edit_new`, `pdfkit_edit_free`, `pdfkit_buffer_free` | 90 |
| `docs/ABI.md` | numbered chapters: the calling rules (UTF-8, `size_t`, handles, threading, ownership of `pdfkit_buffer`), the status codes, the JSON error object with an example, one section per symbol (inputs, outputs, failure codes, "not implemented" until its build file lands), the versioning rule (`pdfkit_abi_version` is 1 now and is raised on every addition), the CLI exit-code mapping (0 ok, 1 usage, 2 refused, 3 self-check, 4 internal, and `PDFKIT_E_NOT_IMPLEMENTED` mapped to exit 4 because a missing function is a defect from the caller's point of view) | 260 |

### 2.5 The CLI (`cli/`)

| File | What it holds | Lines |
|---|---|---|
| `cli/main.cpp` | argument parsing (verb first, `--help` per verb), the verb table, JSON on standard output, exit codes of `docs/ABI.md`, reading the input file the user named into bytes (the CLI is the one place a path is read, `CLAUDE.md` §9), and a JSON usage error naming the verb, what was expected and an example | 180 |
| `cli/verbs/<verb>.cpp` | `inspect`, `text`, `images`, `metadata`, `redact`, `select_pages`, `check`, `channels`, `dataset`; each parses its options, calls the C ABI, prints the buffer, maps the status to an exit code | 9 × 40 |
| `cli/CMakeLists.txt` | `pdfkit_cli` linking the `pdfkit` shared library through its import library only, never `pdfkit_core`, so the CLI proves the ABI | 20 |

`build/check_layout.py` asserts that no file under `cli/` includes a path under
`src/`.

### 2.6 Tests

| File | What it holds | Lines |
|---|---|---|
| `third_party/doctest/` | `doctest.h` at a pinned release, its `LICENSE.txt` (MIT), and a row in `third_party/inventory.tsv` with the upstream archive's SHA-256 | vendored |
| `tests/unit/main.cpp` | the doctest runner | 10 |
| `tests/unit/platform/*_test.cpp` | `checked`, `budget`, `buffer`, `error` (an empty message part is refused), `json_writer` (escapes, number formatting, determinism), `json_reader` (every JSON type, depth limit, the error names the offset) | 500 |
| `tests/unit/api/abi_test.cpp` | every catalogue symbol called with null arguments returns `PDFKIT_E_INVALID_ARGUMENT`, with valid arguments returns `PDFKIT_E_NOT_IMPLEMENTED` and a JSON error whose three fields are non-empty; `pdfkit_buffer_free` on an empty buffer is a no-op | 150 |
| `tests/unit/CMakeLists.txt` | `pdfkit_tests` linking `pdfkit_core`, registered with `ctest` under label `unit` | 20 |
| `tests/abi/dotnet/AbiHello.csproj`, `Program.cs` | a `net10.0` console app with a `[DllImport("pdfkit")]` (or `LibraryImport`) of `pdfkit_abi_version` and `pdfkit_version`/`pdfkit_buffer_free`, printing the version JSON and exiting 0 when the ABI version matches the expected one passed as an argument | 50 |
| `tests/abi/run.py` | builds and runs the .NET app against `out/<preset>/pdfkit.dll`, then the Python check (`ctypes.CDLL(path).pdfkit_abi_version()`), and fails naming which caller could not load the library and why | 80 |

### 2.7 Checks, hooks, CI, packaging (`build/`, `.github/`)

Each check script prints, on failure, the file and line, what was expected and
how to fix it (`CLAUDE.md` §9).

| File | What it checks | Lines |
|---|---|---|
| `build/check.py` | runs every check below in order; `--fast` skips clang-tidy and the build; exit code non-zero on the first failing group, with a summary of all | 120 |
| `build/check_layout.py` | the folder layout of `CLAUDE.md` §3.5; every source under `src/<layer>/` has namespace `pdfkit::<layer>` (plus sub-folder namespaces); one public class per header, named after the file in `snake_case` → `CamelCase`; the include rules of `CLAUDE.md` §3.1 per layer; `cli/` includes only `pdfkit/pdfkit.h`; no OS header (`windows.h`, `unistd.h`, …) and no `_WIN32`, `__linux__`, `__GNUC__` macros outside `src/platform/` | 250 |
| `build/check_headers.py` | the SPDX and copyright lines and a purpose header (a comment block of at least one sentence after the licence lines) on every file under `include/`, `src/`, `cli/`, `tests/`, `build/`, `dataset/tools/` with the extensions `.h .hpp .cpp .py .ps1 .cmake .cs`, and `CMakeLists.txt`; vendored folders exempt | 120 |
| `build/check_comments.py` | no comment naming a phase (`phase \d`, `build-\d`), a ticket, a change order, and no delta words (`used to`, `no longer`, `for now`, `not yet`, `TODO` without an owner question reference) in our sources | 90 |
| `build/check_catalogue.py` | `CLAUDE.md` §2's table, the symbols in `include/pdfkit/pdfkit.h`, the rows of `src/api/catalogue.cpp`, the files under `src/api/` and the verbs under `cli/verbs/` agree; the build file named in each row exists | 160 |
| `build/check_third_party.py` | every folder under `third_party/` has a row in `inventory.tsv` (path, name, version, upstream URL, licence, SHA-256 of the upstream archive, test-only flag) and a licence file; the licence is in the allowed list of `CLAUDE.md` §6; the recorded hash matches the archive when the archive is present in `build/cache/third_party/`, otherwise the hash of the vendored tree (a second column) matches | 150 |
| `build/check_network.py` | no socket, `WSAStartup`, `getaddrinfo`, `URLDownloadToFile`, `InternetOpen`, `WinHttp`, `curl_`, `http://`/`https://` string used as a fetch target, and no link to `ws2_32`, `wininet`, `winhttp` under `include/`, `src/`, `cli/`; the exemptions (`dataset/tools/fetch.py`, `tests/pdfium/fetch.py`) are listed by path with their reason, and the script fails when an exempted file no longer exists | 110 |
| `build/check_repo.py` | no committed file whose first bytes are `%PDF` (via `git ls-files`), no file over 5 MB outside `third_party/` | 60 |
| `build/check_paths.py` | every backticked path in `CLAUDE.md`, `docs/*.md` and `dataset/README.md` resolves, or is listed in `build/paths_pending.tsv` with the phase that creates it; a pending row whose path now exists fails, so each phase shrinks the list | 120 |
| `build/paths_pending.tsv` | the paths `CLAUDE.md` and `docs/` cite that later phases create (`dataset/manifest.tsv`, `dataset/taxonomy.tsv`, `dataset/README.md`, `tests/pdfium/fetch.py`, `tests/dataset/run.py`, `src/writer/channel_table.cpp`, `tests/unit/support/pdf_builder.hpp`, …), each with its phase number | 30 |
| `build/check_format.py` | `clang-format --dry-run --Werror` over our sources; `clang-tidy -p out/mingw-debug` over our sources (skipped with `--fast`) | 70 |
| `build/check_artefact.py` | `objdump -p` over a built `pdfkit.dll` and `pdfkit.exe`: only operating-system DLLs imported (the allowed list in the script with its reason), every catalogue symbol exported undecorated | 80 |
| `build/hooks/pre-push`, `build/install_hooks.py` | the hook runs `python build/check.py --fast` and `ctest --preset unit`; the installer sets `core.hooksPath` to `build/hooks` | 40 |
| `build/package.py`, `build/budgets.tsv` | builds `mingw-release`, collects the files of `CLAUDE.md` §10 into `dist/pdfkit-<version>-windows-x64.zip`, checks each size against `budgets.tsv` (initial budgets: DLL 8 MB, EXE 1 MB, zip 6 MB, to be tightened by phase 10) | 120 |
| `.github/workflows/check.yml` | on push and pull request: `windows-latest`, install WinLibs MinGW of the pinned version, run `build/check.py` and the unit and abi tiers; the file's header states that the hosted runners do not start for this repository and the pre-push hook is the gate | 60 |

## 3. Tests

- **Unit**: the platform and ABI tests of §2.6, label `unit`.
- **Sanitizer**: the same tests under the `mingw-sanitize` preset with whatever
  sanitizers the spike recorded as working.
- **ABI**: `python tests/abi/run.py` (.NET and Python).
- **Dataset and PDFium**: none; phase 2 builds them.
- **Checks**: `python build/check.py` over the whole tree, the checks
  themselves tested by a small `build/tests/test_checks.py` that runs each
  check over a temporary tree with one planted violation and asserts it fails
  naming the file (one case per check).

## 4. Gate

There is no dataset yet, so this phase's gate is the working frame:

| Measure | Required | Command |
|---|---|---|
| all checks | green over the tree | `python build/check.py` |
| unit tier | green, debug and sanitize presets | `ctest --preset unit`, `ctest --preset sanitize` |
| artefact imports | `pdfkit.dll` and `pdfkit.exe` import only operating-system DLLs | `python build/check_artefact.py out/mingw-release` |
| ABI from .NET and Python | both print ABI version 1 | `python tests/abi/run.py` |
| CLI error shape | `pdfkit inspect missing.pdf` exits 1 with a JSON error whose `failed`, `expected` and `fix` are non-empty; `pdfkit text x.pdf` on an existing file exits 4 naming `pdfkit_text` and `build-5.md` | by hand in the session, recorded in the merge message |
| planted violations | each check fails on its planted case | `python -m pytest build/tests` or `python build/tests/test_checks.py` |

The numbers go into the final commit message.

## 5. Kill criterion and the question to the owner

### 5.1 The DLL needs a compiler runtime

**Quantity:** the import table of `pdfkit.dll` built by the pinned toolchain.
**Threshold:** any import that is not an operating-system DLL after the static
runtime flags of §2.1 have been tried (`libstdc++-6.dll`, `libgcc_s_seh-1.dll`,
`libwinpthread-1.dll`).

**Question, word for word:** "The pinned MinGW toolchain cannot build a
pdfkit.dll that loads without shipping its compiler runtime beside it (it imports
<the list>). Which do you want? (a) Install llvm-mingw as a zip in your user
folder (no administrator rights; about 1 hour of session time and $15 to $25 to
re-pin and re-run the checks); (b) ship the three MinGW runtime DLLs beside
pdfkit.dll in every artefact (about 30 minutes and $5 to $10; every consumer
then carries three more files); (c) install the MSVC Build Tools, which needs an
administrator (about 2 hours and $30 to $50 once installed, and a second
toolchain to keep pinned)."

### 5.2 AddressSanitizer is unavailable

Not a kill criterion; an owner question recorded in `OPEN-QUESTIONS.md` when
the spike finds AddressSanitizer unsupported: "The pinned MinGW GCC supports
UBSan (trap mode) but not AddressSanitizer on Windows. Do you want
llvm-mingw (a zip in your user folder, no administrator rights) added as a
second toolchain for the sanitizer tier only? (a) Yes: about 1 to 2 hours and
$20 to $35 to pin it and add the `clang-sanitize` preset, then every phase's
sanitizer run uses it; (b) No: the sanitizer tier stays UBSan-only and memory
errors are caught by the bounds-checked helpers and the dataset's crash count."

### 5.3 The unattended action

When 5.1 trips: write the question with the evidence (the `objdump` output and
the flags tried) into `OPEN-QUESTIONS.md`; take option (b) so that phases 2 and
3 can proceed (the runtime DLLs copied beside `pdfkit.dll` in `out/` and in the
package, `check_artefact.py` listing them as a named exemption with the
question's reference); merge the branch if the checks and the unit tier are
green, with trailer `Build-Phase: 1 gate-failed`; phases that depend on it
proceed and report their gates with that caveat. 5.2 never blocks.

## 6. Rollback

Everything this phase adds is new; reverting the merge commit returns the
repository to the charter-only state. Nothing outside the repository changes
except the `--user` pip install of clang-format and clang-tidy and the
`core.hooksPath` setting, both undone by `pip uninstall` and
`git config --unset core.hooksPath`.

## 7. Execution order inside the phase

1. The spike (§2.1): measure, write `build/toolchain.md`, install the pinned
   clang tools. If the static-runtime DLL fails, apply §5.3 and continue.
2. `CMakeLists.txt`, presets, warnings, the layer folders with placeholders;
   build an empty `pdfkit_core`.
3. Platform layer, test first: `error`, `checked`, `budget`, `buffer`,
   `json_writer`, `json_reader`.
4. The header, `guarded`, handles, the registry, the lifecycle symbols, the
   thirteen stub functions, `abi_test.cpp`.
5. The CLI and its verbs.
6. The ABI tier (.NET and Python).
7. The check scripts with their planted-violation tests, then `check.py`, the
   hook, CI, packaging and budgets.
8. `docs/ABI.md`, written from what landed.
9. The full gate of §4; the numbers into the last commit message.

**Split point:** after step 6 (the hello loads from .NET and Python) pushed with
`Build-Phase: 1 incomplete`; the second session writes the check scripts.
Phases 2 and 3 need the checks, so they do not start on an incomplete phase 1.

## 8. Opening prompt

> Read `CLAUDE.md` whole, `docs/DESIGN.md` chapter 19 (the refusal and warning
> codes the status codes must carry), and all of `build-1.md`; this is the
> first phase, so there are no earlier deviations to read. Work in a worktree
> on branch `phase/1-skeleton` created from `main`. The toolchain is already on
> the laptop (WinLibs MinGW-w64 GCC under
> `C:\Users\oliber001\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin`,
> CMake and Ninja in the same folder, Python 3.12, .NET SDK 10); you have no
> administrator rights, so install clang-format and clang-tidy with
> `pip install --user`. Start with the spike of §2.1 and record what the commands
> print, never what this file expects. Build the skeleton of §2 in the order of
> §7, failing tests first. Every file carries the licence header and a purpose
> header; every external fact you rely on carries its source and whether you
> read it or recalled it, in the comment and in `docs/SOURCES.md`. Run
> `python build/check.py` and `ctest --preset unit` once they exist and again
> before finishing. When the toolchain cannot build a runtime-free DLL, follow
> §5.3 and do not wait for the owner. Finish with a last commit whose message
> carries the gate numbers of §4 and the trailer `Build-Phase: 1 done` (or
> `incomplete` or `gate-failed` with the reason), push the branch, and never
> merge to `main`: the team lead merges. Record any deviation from this file
> in §9 and in every later build file it affects, in the same change.

## 9. Deviations recorded by this phase

1. **`.gitattributes`** (team lead's instruction): every text file is stored
   and checked out LF (`* text=auto eol=lf`, binary patterns listed), because
   the laptop's `core.autocrlf` breaks clang-format and the shell hook. Its own
   commit; the index already held LF, so no blob changed.
2. **Files added beyond §2's lists**, each to keep one public class per header
   or one concern per file: `src/platform/json_value.{hpp,cpp}` (`JsonValue`,
   the tree `JsonReader` returns), `src/platform/utf8_sequence.{hpp,cpp}`
   (UTF-8 decoding shared by the JSON writer and reader),
   `src/platform/build_info.{hpp,cpp}` (version, compiler and build type for
   `pdfkit_version`, so compiler macros stay in the platform layer),
   `src/api/arguments.{hpp,cpp}` (the argument checks), `cli/arguments.*`,
   `cli/json_text.*`, `cli/output.*`, `cli/support.*`, `cli/verbs/verbs.hpp`,
   `cli/pdfkit.manifest` and `cli/pdfkit.rc`, `build/runtime.cmake`,
   `build/llvm-mingw.cmake`, `build/checklib.py` (helpers shared by the
   checks). `checked.hpp` stays header-only.
3. **The export attribute**: the public header marks every declaration with
   `PDFKIT_API`, empty for callers; the core's build defines
   `PDFKIT_EXPORT_HEADER="platform/export.hpp"`, which the header includes, so
   the attribute is spelled only in `src/platform/export.hpp` and every
   declaration carries it whatever a source's include order. Clang refuses
   `dllexport` added on a redeclaration, which the plan's
   attribute-on-the-definition shape needed. The static core is compiled with
   the attribute (`PDFKIT_BUILDING` is public on `pdfkit_core`), not without
   it as §2.3 said, because its objects are the DLL's objects; an executable
   linking the core exports the same names, which is harmless. The shared
   library is built from `$<TARGET_OBJECTS:pdfkit_core>`.
4. **Signatures outside the uniform shape** of §2.4: `pdfkit_write(edit,
   options_json, pdfkit_buffer* out_pdf, pdfkit_buffer* out)` has two output
   buffers, because the PDF bytes and the write report are both outputs;
   `pdfkit_edit_new(doc, pdfkit_edit** out_edit, pdfkit_buffer* out)` returns
   a status and the handle through `out_edit`. An optional JSON argument may be
   `NULL` (meaning `{}`); the required ones (`matches_json`,
   `treatments_json`, `fields_json`, `pages_json`, `strings_json`) refuse it.
   `bytes` may be `NULL` when `size` is 0, so an empty input reaches `open` and
   is refused as `not_pdf` rather than as a bad argument.
5. **`platform::ErrorCode` gains `self_check`** beyond "chapter 19 plus
   internal, not_implemented, invalid_argument", because
   `PDFKIT_E_SELF_CHECK` needs a code (`docs/DESIGN.md` §18).
6. **`pdfkit_version` returns a `catalogue` array** beside `version`, `abi` and
   `build`: per catalogue row its number, function, symbol, verb, build file,
   `implemented`, and for an unimplemented row the exact error object the
   function returns. The CLI reads it to report a missing function before
   opening the document, which is how `pdfkit text x.pdf` names
   `pdfkit_text` and `build-5.md` while `pdfkit_open` is also missing. The
   ABI version stays 1. `CLAUDE.md` §2's lifecycle sentence does not list
   `pdfkit_version`; `build/check_catalogue.py` names it as the one extra
   symbol, and `OPEN-QUESTIONS.md` asks to add it to the charter.
7. **The CLI** carries its own small JSON reader (it may include nothing of the
   library but the header), takes `--options` and `--open-options` JSON on the
   document verbs, reads files with C stdio and uses no `std::filesystem`
   (which tripled `pdfkit.exe` to 3.1 MB against the 1 MB budget), and embeds a
   manifest setting the UTF-8 code page so non-ASCII paths work. `images`
   does not pre-check its `--out` folder; the first write reports it. The
   redact job format is fixed in `docs/ABI.md` §6. Usage errors carry `code`
   `usage`; an output that cannot be written, `output_failed`.
8. **The sanitizer tier is two runs**: `ctest --preset sanitize` (GCC, UBSan in
   trap mode, the only sanitizer GCC supports here) and `ctest --preset
   clang-sanitize` (llvm-mingw 20261020, ASan and UBSan), taking the
   recommended answer of the §5.2 question while it stands. llvm-mingw lives in
   the main checkout's `build/cache/llvm-mingw/`; `build/toolchain.md` has the
   fetch command and hash. GCC stays the pinned compiler of every artefact.
9. **Release links** add `-ffunction-sections -fdata-sections`,
   `-Wl,--gc-sections`, `-s` and `-Wl,--no-insert-timestamp`: the DLL is
   634 KB and the exe 333 KB, and a rebuild gives the same bytes.
10. **`CMakePresets.json` uses schema version 11**, the newest CMake 4.3.2
    accepts (12 is refused), and turns off C++20 module scanning, which the
    sources do not use and which put module flags clang-tidy rejects into the
    compile database.
11. **Third-party inventory**: columns `path, name, version, upstream,
    archive, archive_sha256, tree_sha256, licence (SPDX), test_only`. The tree
    hash is the SHA-256 over `<path>\0<sha256 of bytes>\n` of every file in
    sorted order, our `CMakeLists.txt` excluded; it is always checked, and the
    archive hash too when the archive is in the main checkout's
    `build/cache/third_party/` (the plan checked one or the other). Inventory
    names must appear in `THIRD-PARTY-NOTICES.md`; the converse is not checked,
    because the notices list components not vendored yet. doctest is 2.5.3,
    the newest release on 2026-10-24.
12. **Check details the plan left open**: `check_network.py` also reads the
    Python tools under `build/`, `tests/` and `dataset/tools/` for imports of
    network modules (the two named exemptions only make sense so), allows XMP
    and RDF namespace URIs as string literals, and accepts a missing exempted
    file while `build/paths_pending.tsv` lists it; `check_comments.py` allows
    "is/are/be/being used to" (present tense) and a to-do only as
    `TODO(OPEN-QUESTIONS.md <section>)`; `check_catalogue.py` allows helper
    files `cli/verbs/<verb>_<part>.cpp` (build-4 adds `dataset_fonts.cpp`);
    `check_artefact.py` also fails on an export the header does not declare;
    `check_paths.py` resolves a path from the root, the document's folder,
    `src/` and `include/`, accepts a stem without its extension, and accepts
    git-ignored paths. `check.py` runs every group and summarises them all
    rather than stopping at the first failure.
13. **Pending paths owned by the merge**: `docs/archive/` and
    `docs/archive/build/` are listed in `build/paths_pending.tsv` with phase 1;
    the merge that archives this file removes both rows. Phase 2's rows
    (`dataset/...`, `tests/pdfium/...`, `tests/dataset/run.py`) are removed
    when its files land.
14. **Downstream build files**: the deviations that touch a later phase are
    noted at the end of `build-3.md` to `build-10.md`. `build-2.md` is not
    edited, because phase 2 runs in parallel and edits its own §9; its session
    reads this section (its §1 says so).
