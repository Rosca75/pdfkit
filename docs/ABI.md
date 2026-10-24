# ABI.md

This file owns the C ABI of pdfkit: every exported symbol, its inputs and
outputs, its status codes, the JSON error object, the versioning rule, and the
command-line tool's verbs and exit codes. The header is
`include/pdfkit/pdfkit.h`; `CLAUDE.md` §2 owns which functions exist, and this
file owns how they are called. A function's JSON shapes are added here by the
change that implements it.

## 1. Calling rules

- **Strings** are UTF-8 and NUL-terminated. A JSON argument is a JSON object
  read strictly (RFC 8259: no comments, no trailing commas, no duplicate keys,
  well-formed UTF-8, nesting at most 64 deep, at most 64 MiB).
- **Sizes** are `size_t`; integers in JSON keep their exact value up to 2^63.
- **Handles** are opaque pointers: `pdfkit_document*` from `pdfkit_open`,
  released by `pdfkit_close`; `pdfkit_edit*` from `pdfkit_edit_new`, released
  by `pdfkit_edit_free`, always before its document is closed.
- **Threading**: a handle is used by one thread at a time; distinct handles are
  independent. The library holds no global mutable state, so this is the whole
  contract.
- **Buffers**: every function returning `pdfkit_status` writes its result, or on
  failure its JSON error object, into the `pdfkit_buffer` the caller passes.
  The caller declares the buffer zeroed (`pdfkit_buffer out = {0};`), passes its
  address, reads `data` and `size`, and frees it with `pdfkit_buffer_free`,
  which zeroes it again. `data` is followed by a zero byte not counted in
  `size`, so a JSON result reads as a C string. A call overwrites the buffer
  without freeing what it held, so a buffer is freed before it is reused.
- **No exception crosses the ABI**: every exported body runs inside a
  catch-all that turns any failure into a status and an error object. An
  out-of-memory condition is `PDFKIT_E_INTERNAL`; when even the error object
  cannot be allocated, the status alone is returned and the buffer stays empty.
- **Calling convention**: the platform's C default (`cdecl` on Windows x64,
  where there is one convention). From .NET: `[DllImport("pdfkit")]` with
  `CallingConvention.Cdecl`; from Python: `ctypes.CDLL`.
- **Linking**: `pdfkit.dll` imports only operating-system DLLs (KERNEL32 and the
  Universal C Runtime API sets). C and C++ callers link `pdfkit.lib` (the import
  library) and include `pdfkit/pdfkit.h` without defining anything.

## 2. Status codes

| Value | Status | Meaning | Error codes it carries |
|---|---|---|---|
| 0 | `PDFKIT_OK` | the call succeeded; the buffer holds the result | none |
| 1 | `PDFKIT_E_INVALID_ARGUMENT` | a null handle or required argument, or malformed JSON input | `invalid_argument` |
| 2 | `PDFKIT_E_REFUSED` | the input document is refused | `not_pdf`, `no_page`, `password_required`, `unsupported_security_handler`, `limit_exceeded`, `page_ceiling` (`docs/DESIGN.md` §19) |
| 3 | `PDFKIT_E_SELF_CHECK` | the self-check found a redacted string in the output; no output is returned | `self_check` (`docs/DESIGN.md` §18) |
| 4 | `PDFKIT_E_NOT_IMPLEMENTED` | the catalogue function is not part of this build | `not_implemented` |
| 5 | `PDFKIT_E_INTERNAL` | a defect in pdfkit | `internal` |

The values never change. A new refusal code is added to `docs/DESIGN.md` §19
and rides under `PDFKIT_E_REFUSED`; a new status value is an ABI addition
(§5). Warnings (`repaired`, `damaged_stream`, `unreadable_text`, and the rest of
`docs/DESIGN.md` §19) never fail a call: they are listed in the result's
`warnings` array, each an object with a `code` and the fields the design names.

## 3. The JSON error object

Every failure writes one object with four string fields, each non-empty: the
library cannot build an error without them.

```json
{
  "code": "invalid_argument",
  "failed": "pdfkit_text options_json: the JSON input is malformed at byte 13: found ']' where a value was expected",
  "expected": "a JSON value: an object, an array, a string, a number, true, false or null",
  "fix": "correct the input at byte 13; it must be strict JSON (RFC 8259): double-quoted strings, no comments, no trailing commas, no leading zeros, UTF-8 text"
}
```

| Field | Holds |
|---|---|
| `code` | the stable code of §2, for programs |
| `failed` | what failed, naming the symbol, the parameter, the object, the page or the byte offset |
| `expected` | what the library expected there |
| `fix` | what the caller can do about it |

A function not part of this build answers with `code` `not_implemented`, a
`failed` naming the symbol, its catalogue number and its function, and an
`expected` naming the build file that delivers it:

```json
{
  "code": "not_implemented",
  "failed": "pdfkit_text (catalogue function 2, Extract text) is not implemented in this build of pdfkit",
  "expected": "a pdfkit build that implements pdfkit_text, which build-5.md delivers",
  "fix": "use a pdfkit build whose pdfkit_version output lists pdfkit_text with \"implemented\": true"
}
```

## 4. The symbols

Each function checks its arguments first (a null `out`, handle or required
string, malformed JSON), then does its work. An optional JSON argument may be
`NULL`, which means `{}`.

### 4.1 Lifecycle

| Symbol | Signature | Behaviour |
|---|---|---|
| `pdfkit_abi_version` | `uint32_t pdfkit_abi_version(void)` | the ABI version, 1 |
| `pdfkit_version` | `pdfkit_status pdfkit_version(pdfkit_buffer* out)` | the version object below |
| `pdfkit_buffer_free` | `void pdfkit_buffer_free(pdfkit_buffer* buffer)` | frees and zeroes the buffer; a zeroed buffer or `NULL` is a no-op |
| `pdfkit_close` | `void pdfkit_close(pdfkit_document* doc)` | destroys the document; `NULL` is a no-op |
| `pdfkit_edit_new` | `pdfkit_status pdfkit_edit_new(pdfkit_document* doc, pdfkit_edit** out_edit, pdfkit_buffer* out)` | creates an empty edit plan over `doc`; `out` receives `{}`; `PDFKIT_E_INVALID_ARGUMENT` for a null `doc` or `out_edit` |
| `pdfkit_edit_free` | `void pdfkit_edit_free(pdfkit_edit* edit)` | destroys the edit plan; `NULL` is a no-op |

The version object:

```json
{
  "version": "0.1.0",
  "abi": 1,
  "build": "gcc 16.1.0, Release",
  "catalogue": [
    {"number": "1.1", "function": "Open", "symbol": "pdfkit_open", "verb": "inspect",
     "build_file": "build-3.md", "implemented": false,
     "error": {"code": "not_implemented", "failed": "...", "expected": "...", "fix": "..."}}
  ]
}
```

`catalogue` holds one entry per row of `CLAUDE.md` §2 in its order; `error` is
present only when `implemented` is false and is exactly the object the
function returns, so a caller (the CLI among them) can report a missing
function before reading any input. `build` names the compiler and the build
type and never a time, so two builds of one tree report the same text.

### 4.2 Catalogue functions

| # | Signature | Required arguments | Output on success | Delivered by |
|---|---|---|---|---|
| 1.1 | `pdfkit_open(const uint8_t* bytes, size_t size, const char* options_json, pdfkit_document** out_doc, pdfkit_buffer* out)` | `out`, `out_doc`; `bytes` unless `size` is 0 | `*out_doc` the handle; `out` the open report | `build-3.md` |
| 1.2 | `pdfkit_inspect(pdfkit_document* doc, const char* options_json, pdfkit_buffer* out)` | `doc`, `out` | the inspect JSON | `build-3.md` |
| 2 | `pdfkit_text(pdfkit_document* doc, const char* options_json, pdfkit_buffer* out)` | `doc`, `out` | the text JSON | `build-5.md` |
| 3.1 | `pdfkit_images(pdfkit_document* doc, const char* options_json, pdfkit_buffer* out)` | `doc`, `out` | the image list JSON | `build-6.md` |
| 3.2 | `pdfkit_image_data(pdfkit_document* doc, const char* identity, const char* options_json, pdfkit_buffer* out)` | `doc`, `identity`, `out` | PNG or JPEG bytes (not JSON) | `build-6.md` |
| 4 | `pdfkit_metadata(pdfkit_document* doc, const char* options_json, pdfkit_buffer* out)` | `doc`, `out` | the metadata JSON | `build-10.md` |
| 5 | `pdfkit_redact_text(pdfkit_edit* edit, const char* matches_json, pdfkit_buffer* out)` | `edit`, `matches_json`, `out` | the per-match JSON | `build-8.md` |
| 6 | `pdfkit_redact_images(pdfkit_edit* edit, const char* treatments_json, pdfkit_buffer* out)` | `edit`, `treatments_json`, `out` | the per-treatment JSON | `build-9.md` |
| 7 | `pdfkit_set_metadata(pdfkit_edit* edit, const char* fields_json, pdfkit_buffer* out)` | `edit`, `fields_json`, `out` | the fields as they will be written | `build-10.md` |
| 8 | `pdfkit_channels(pdfkit_document* doc, const char* options_json, pdfkit_buffer* out)` | `doc`, `out` | the channel report JSON | `build-7.md` |
| 9 | `pdfkit_select_pages(pdfkit_edit* edit, const char* pages_json, pdfkit_buffer* out)` | `edit`, `pages_json`, `out` | `{}` | `build-7.md` |
| 10.1 | `pdfkit_write(pdfkit_edit* edit, const char* options_json, pdfkit_buffer* out_pdf, pdfkit_buffer* out)` | `edit`, `out_pdf`, `out` | `out_pdf` the PDF bytes; `out` the write report | `build-7.md`, self-check `build-8.md` |
| 10.2 | `pdfkit_check(const uint8_t* bytes, size_t size, const char* strings_json, pdfkit_buffer* out)` | `strings_json`, `out`; `bytes` unless `size` is 0 | the surviving occurrences JSON | `build-8.md` |

Every function returns `PDFKIT_E_INVALID_ARGUMENT` for a missing required
argument or malformed JSON, and `PDFKIT_E_NOT_IMPLEMENTED` until the build file
in the last column lands; the build file that implements a function adds its
JSON shapes and its failure codes to a section of this chapter. `pdfkit_write`
is the one function with two output buffers: the PDF bytes are not JSON, and
the write report travels beside them; on failure `out_pdf` stays empty and
`out` holds the error.

## 5. Versioning

- The ABI only grows: a symbol never changes its signature or its meaning, a
  JSON output only gains fields, and a status value never changes.
- `pdfkit_abi_version` returns 1 and is raised by one on every addition to the
  header (a symbol or a status value), in the change that adds it.
- A caller checks `pdfkit_abi_version() >= N` for the version whose symbols it
  binds, and reads `pdfkit_version`'s `catalogue` to learn which functions this
  build implements.

## 6. The command-line tool

`pdfkit <verb> [arguments]` writes one JSON document on standard output and
exits with a stable code. `pdfkit --help` lists the verbs, `pdfkit <verb> --help`
prints a verb's usage and an example, and `pdfkit --version` prints the
version object of §4.1. The tool calls the C ABI and nothing beneath it.

| Verb | Arguments | Catalogue functions |
|---|---|---|
| `inspect` | `<file.pdf> [--options <json>] [--open-options <json>]` | 1.2, 1.1 |
| `text` | `<file.pdf> [--options <json>] [--open-options <json>]` | 2, 1.1 |
| `images` | `<file.pdf> [--out <folder>] [--options <json>] [--open-options <json>]` | 3.1, 3.2, 1.1 |
| `metadata` | `<file.pdf> [--options <json>] [--open-options <json>]` | 4, 1.1 |
| `redact` | `<job.json>` | 5, 6, 7, 9 (each section present), 10.1, 1.1 |
| `select-pages` | `<file.pdf> --pages <json> --out <output.pdf> [--open-options <json>]` | 9, 10.1, 1.1 |
| `check` | `<file.pdf> --strings <json>` | 10.2 |
| `channels` | `<file.pdf> [--options <json>] [--open-options <json>]` | 8, 1.1 |
| `dataset` | `--manifest <manifest.tsv> --out <folder>`, or `--file <file.pdf> --id <id>` | every function the manifest expects |

The redact job file is a JSON object: `input` and `output` (paths), an optional
`open_options` object, and at least one of the sections `text`, `images`,
`metadata` and `pages`, each handed byte for byte to its function
(`pdfkit_redact_text`, `pdfkit_redact_images`, `pdfkit_set_metadata`,
`pdfkit_select_pages`) on one edit plan. An output file is written only when
`pdfkit_write` succeeds.

Order of work: the verb parses its arguments, reads its input file, then asks
`pdfkit_version` whether every function it needs is implemented (its own
function first) and only then opens the document, so a missing function is
reported without reading the document.

### 6.1 Exit codes

| Code | Meaning | Statuses |
|---|---|---|
| 0 | success | `PDFKIT_OK` |
| 1 | usage error: a wrong command line, a file that cannot be read or written, a malformed option | `PDFKIT_E_INVALID_ARGUMENT` |
| 2 | the input was refused; the JSON names why | `PDFKIT_E_REFUSED` |
| 3 | the self-check refused the output | `PDFKIT_E_SELF_CHECK` |
| 4 | an internal error, or a function this build lacks | `PDFKIT_E_INTERNAL`, `PDFKIT_E_NOT_IMPLEMENTED` |

`PDFKIT_E_NOT_IMPLEMENTED` maps to 4 because a missing function is a defect
from the caller's point of view: the build in hand cannot do what was asked.

### 6.2 Errors the tool finds itself

A usage error, a file the tool cannot read or write, and a mismatch between
`pdfkit.exe` and `pdfkit.dll` are reported in the four-field shape of §3, with
`code` `usage`, `output_failed` or `internal` and `failed` naming the verb:

```json
{"code":"usage","failed":"pdfkit inspect: cannot read missing.pdf: No such file or directory","expected":"the path of a readable file","fix":"check the path and that the file exists and is not open elsewhere"}
```

Paths are UTF-8: the executable's manifest sets the process code page to UTF-8,
so a path in any script reaches the file system intact.
