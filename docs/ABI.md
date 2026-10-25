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
| 9 | `pdfkit_select_pages(pdfkit_edit* edit, const char* pages_json, pdfkit_buffer* out)` | `edit`, `pages_json`, `out` | `{"pages": [...]}`, the pages recorded | `build-7.md` |
| 10.1 | `pdfkit_write(pdfkit_edit* edit, const char* options_json, pdfkit_buffer* out_pdf, pdfkit_buffer* out)` | `edit`, `out_pdf`, `out` | `out_pdf` the PDF bytes; `out` the write report | `build-7.md`, self-check `build-8.md` |
| 10.2 | `pdfkit_check(const uint8_t* bytes, size_t size, const char* strings_json, pdfkit_buffer* out)` | `strings_json`, `out`; `bytes` unless `size` is 0 | the surviving occurrences JSON | `build-8.md` |

Every function returns `PDFKIT_E_INVALID_ARGUMENT` for a missing required
argument or malformed JSON, and `PDFKIT_E_NOT_IMPLEMENTED` until the build file
in the last column lands; the build file that implements a function adds its
JSON shapes and its failure codes to a section of this chapter. `pdfkit_write`
is the one function with two output buffers: the PDF bytes are not JSON, and
the write report travels beside them; on failure `out_pdf` stays empty and
`out` holds the error.

### 4.3 pdfkit_open (catalogue 1.1)

`pdfkit_open` copies the bytes, finds the `%PDF-` header within the first
1,024 bytes, reads the cross-reference table along the trailer chain or
rebuilds it by scanning when that path disappoints, decrypts with the empty
user password, parses every object once (so every recovery the file needs is
named at open) and walks the page tree (`docs/DESIGN.md` chapters 1 to 4).

The options are the limits of `docs/DESIGN.md` §3.2, each a positive integer;
any other key, or a value that is not a positive integer, is
`invalid_argument`:

| Option | Default | Bounds |
|---|---|---|
| `stream_bytes` | 536870912 (512 MiB) | the decoded bytes of one stream |
| `document_bytes` | 2147483648 (2 GiB) | the decoded bytes of the whole document |
| `objects` | 8388607 | the objects the cross-reference table or the scan names |
| `page_ceiling` | 10000 | the pages of the document |
| `operators_per_page` | 50000000 | the content operators of one page |

On success `*out_doc` receives the handle and `out` the open report:

```json
{
  "version": "1.7",
  "page_count": 3,
  "encryption": {"filter": "Standard", "version": 4, "revision": 4, "method": "AESV2",
                 "key_bits": 128, "encrypt_metadata": true},
  "repairs": ["the file was reconstructed by scanning it, because startxref 1234 does not land on xref or a cross-reference stream"],
  "warnings": [{"code": "repaired", "message": "...", "page": null, "reasons": ["..."]},
               {"code": "damaged_stream", "message": "...", "page": 2}]
}
```

| Key | Holds |
|---|---|
| `version` | the header's version, raised by a later catalog `/Version`; empty when no header was found |
| `page_count` | the pages in tree order |
| `encryption` | `null` for an unencrypted file; otherwise the standard handler's version (`/V`), revision (`/R`), method (`RC4`, `AESV2`, `AESV3` or `Identity`), key length in bits and whether the metadata is encrypted |
| `repairs` | every recovery the file needed, each named once, in the order met; at most 100, the last line then counting the rest; empty for a file read as written |
| `warnings` | the warnings of `docs/DESIGN.md` chapter 19 met while opening, each with `code`, `message` and `page` (`null` when it concerns no page); when `repairs` is not empty the first is `repaired`, which also carries the `reasons` |

Failures, each with the error object of §3: `invalid_argument` (a null `out` or
`out_doc`, `bytes` null with a non-zero `size`, malformed or unknown options),
and the refusals `not_pdf`, `no_page`, `password_required`,
`unsupported_security_handler`, `limit_exceeded` and `page_ceiling` of
`docs/DESIGN.md` chapter 19 under `PDFKIT_E_REFUSED`. On failure `*out_doc` is
`NULL`.

### 4.4 pdfkit_inspect (catalogue 1.2)

`pdfkit_inspect` reports what open read; it takes no options (`NULL` or `{}`;
anything else is `invalid_argument`) and never fails on a valid handle:

```json
{
  "version": "1.7",
  "page_count": 2,
  "pages": [{"page": 1, "media_box": [0, 0, 612, 792], "crop_box": [10, 20, 300, 400], "rotate": 90}],
  "encryption": null,
  "repairs": [],
  "warnings": []
}
```

`pages` holds one object per page in tree order: its number from 1, the media
box and the crop box as `[left, bottom, right, top]` in points (normalised, the
crop box clipped to the media box, a missing media box being US Letter), and
the rotation (0, 90, 180 or 270). `encryption`, `repairs` and `warnings` are
those of the open report at the time of the call. Keys are written in the order
shown, so two calls give the same bytes.

### 4.5 pdfkit_channels (catalogue 8)

`pdfkit_channels` reports, per row of the channel table (`docs/DESIGN.md`
§15, `src/writer/channel_table.cpp`), whether the channel is present in the
document and how many occurrences it holds; every `pdfkit_write` strips the
same rows. It takes no options (`NULL` or `{}`; anything else is
`invalid_argument`) and never fails on a valid handle.

```json
{
  "channels": [{"id": "ch_revisions", "name": "earlier revisions (incremental updates)", "present": true, "count": 1}],
  "present": ["ch_revisions"],
  "pending_redactions": 0,
  "warnings": [{"code": "pending_redactions", "message": "...", "page": 2}]
}
```

`channels` holds the 23 rows in the table's order, each with its `id` (the
dataset's taxonomy tag), its `name`, `present` and `count`; `present` lists the
ids whose count is above zero. The counts: `ch_revisions` the sections the
`startxref` chain adds after the first (a linearized file's first-page section
discounted); `ch_unreferenced` the objects of the table no reference from the
trailer reaches (cross-reference and object streams aside); `ch_thumbnail`,
`ch_web_capture` the pages carrying the key (plus one for `/SpiderInfo`);
`ch_pieceinfo`, `ch_last_modified`, `ch_object_metadata` the dictionaries
carrying the key (the catalog's `/Metadata` aside); `ch_javascript` the
JavaScript actions; `ch_embedded_file` the `/EF` and `/AF` entries;
`ch_annotation` the annotations of every page; `ch_acroform` the top-level
fields plus one for `/XFA`; `ch_outline` the outline items; `ch_named_dest` the
`/Dests` entries and the leaves of every `/Names` tree; `ch_struct_tree` the
structure elements; `ch_optional_content` the groups of `/OCGs`;
`ch_page_labels` the label ranges; `ch_xmp_history` 1 when the catalog's
packet holds `xmpMM:History`; `ch_threads` the threads; the catalog keys
(`ch_portfolio`, `ch_perms_legal`, `ch_uri_base`, `ch_search_index`) 1 per key
present; `ch_encrypt` 1 for an encrypted input. A structure that is present
but empty counts 1. `pending_redactions` counts the `/Redact` annotations
never applied, and each page holding one has a `pending_redactions` warning.

### 4.6 pdfkit_select_pages (catalogue 9)

`pdfkit_select_pages` records on an edit plan the pages the output keeps, in
the order given; `pdfkit_write` applies it. The input is
`{"pages": [3, 1, 2]}`, 1-based page numbers of the document as pdfkit reads
it. It is refused with `invalid_argument` (and the three message parts naming
the page and the document's page count) when the list is empty, names a page
twice or a page outside 1 to the page count, holds anything but whole numbers,
or the object holds another key. A later call replaces the list. On success
`out` holds `{"pages": [3, 1, 2]}`.

### 4.7 pdfkit_write (catalogue 10.1)

`pdfkit_write` applies the edit plan in the order of `CLAUDE.md` §3.2 and
returns a fresh, unencrypted file of the reachable objects
(`docs/DESIGN.md` §17) with every channel of §15 stripped and, when pages were
selected, only those pages (§16). It takes no options (`NULL` or `{}`). On
success `out_pdf` holds the PDF bytes and `out` the write report; on failure
`out_pdf` stays empty and `out` holds the error (a refusal met while decoding
a content stream for pruning, such as `limit_exceeded`, is the document's
refusal under `PDFKIT_E_REFUSED`). Writing one plan twice gives the same
bytes.

```json
{
  "page_count": 2,
  "pages": [3, 1],
  "bytes": 18273,
  "sha256": "<hex of the output>",
  "objects": 41,
  "channels": [{"id": "ch_revisions", "stripped": 1}],
  "pending_redactions": 0,
  "missing_references": 0,
  "unpruned_resources": 0,
  "rectangles": [],
  "self_check": {"status": "not_run", "reason": "no text redaction in the edit plan"},
  "warnings": []
}
```

| Key | Holds |
|---|---|
| `page_count`, `pages` | the output's pages and, in output order, the source page each one is |
| `bytes`, `sha256` | the output's size and SHA-256 |
| `objects` | the indirect objects written, the cross-reference stream aside |
| `channels` | per row of the table, in its order, the occurrences the source held, which the output no longer carries (`ch_optional_content` is kept in its minimal form) |
| `pending_redactions` | the source's `/Redact` annotations never applied; the output drops them with every annotation |
| `missing_references` | the distinct object numbers referenced but missing, written as `null` |
| `unpruned_resources` | the resource dictionaries kept whole because their content stream did not read whole |
| `rectangles` | the rectangles a redaction changed, per page; empty when nothing was redacted |
| `self_check` | `{"status": "not_run", "reason": ...}` when the plan redacts no text; the self-check's verdict otherwise (`docs/DESIGN.md` §18) |
| `warnings` | the `pending_redactions` warnings of the source, and `xmp_unreadable` when the catalog's XMP packet did not decode and was dropped |

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
| `select-pages` | `<file.pdf> --pages <list> --out <output.pdf> [--open-options <json>]`, the list being page numbers and ascending ranges separated by commas (`1,3-5`) | 9, 10.1, 1.1 |
| `check` | `<file.pdf> --strings <json>` | 10.2 |
| `channels` | `<file.pdf> [--options <json>] [--open-options <json>]` | 8, 1.1 |
| `dataset` | `--file <file.pdf> --id <id> --out <folder>`, or `--manifest <manifest.tsv> --files <files root> --out <folder>` | every function this build implements (`tests/dataset/outcome.md` owns the outcome) |

The redact job file is a JSON object: `input` and `output` (paths), an optional
`open_options` object, and at least one of the sections `text`, `images`,
`metadata` and `pages`, each handed byte for byte to its function
(`pdfkit_redact_text`, `pdfkit_redact_images`, `pdfkit_set_metadata`,
`pdfkit_select_pages`) on one edit plan. An output file is written only when
`pdfkit_write` succeeds.

The dataset verb writes one outcome per file into `<folder>/outcomes/<id>.json`,
a folder the caller creates (the harness does), in the shape
`tests/dataset/outcome.md` fixes, and prints `{"id", "outcome"}` for one file or
`{"files", "written"}` for a manifest. It exits 0 whenever the outcome was
written, whatever the file's fate, because the refusal is recorded inside it.

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
