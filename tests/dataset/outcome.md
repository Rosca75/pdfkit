# The dataset outcome contract

This file fixes what `pdfkit dataset` writes for one manifest file, so that the
verb (`cli/verbs/dataset.cpp`) and the harness (`tests/dataset/run.py`) and the
PDFium comparison (`tests/pdfium/compare.py`) can be written apart and meet here.
A change to a shape below is a change to all three, made in one commit.

## 1. Invocation

```
pdfkit dataset --file <pdf> --id <id> --out <dir>
pdfkit dataset --manifest <manifest.tsv> --files <files root> --out <dir>
```

- **Per file** (the form the harness uses): reads `<pdf>`, runs every catalogue
  function the build has, writes `<dir>/outcomes/<id>.json` and prints one line
  of JSON on standard output: `{"id": "<id>", "outcome": "<path written>"}`.
- **Per manifest**: the same for every row of the manifest, the file of a row
  being `<files root>/<set>/<id>.pdf`; prints `{"files": n, "written": n}`.
  A row whose file is absent is written as an outcome whose `open.status` is
  `missing`.

Exit codes are those of `docs/ABI.md`: **0** when the outcome was written,
whatever the file's fate (a refusal, a repair, a function not implemented are
all recorded inside the outcome); **1** usage error; **4** internal error, with
the JSON error object (`code`, `failed`, `expected`, `fix`) on standard output.
A verb that is itself not implemented exits 4 with the code
`PDFKIT_E_NOT_IMPLEMENTED`; the harness then counts every column as skipped.

The verb never reads a path other than the one it is given, never writes outside
`<dir>/outcomes/` and `<dir>/outputs/` (both created by the caller), and writes
nothing that depends on time, locale or the path, so two runs over one file
write the same bytes: an output it writes is named in the outcome relative to
`<dir>` (`outputs/<id>.pdf`).

## 2. The outcome file

UTF-8 JSON, LF, no byte-order mark, keys in exactly the order shown, two-space
indentation, one key per catalogue function in the order of the catalogue
(`open`, `inspect`, `text`, `images`, `metadata`, `channels`, `redact`), then
`write` and `select`, each present always: a function the verb did not run is
`null`.

```json
{
  "id": "qpdf-0001",
  "sha256": "<hex of the bytes read>",
  "bytes": 12345,
  "open": {
    "status": "ok",
    "code": null,
    "pages": 3,
    "repairs": ["startxref did not land on a cross-reference section"],
    "warnings": ["repaired"]
  },
  "inspect": {"status": "ok", "code": null, "version": "1.7", "pages": 3},
  "text": {
    "status": "ok",
    "code": null,
    "pages": [{"page": 1, "text": "...", "unreadable": 0, "hidden": 0}],
    "text": "page one text\fpage two text"
  },
  "images": {
    "status": "ok",
    "code": null,
    "count": 4,
    "decoded": 3,
    "undecodable": [{"identity": "img:0123456789abcdef", "code": "undecodable_image"}]
  },
  "metadata": {"status": "ok", "code": null, "agree": true},
  "channels": {"status": "ok", "code": null, "present": ["ch_annotation"]},
  "redact": {
    "status": "ok",
    "code": null,
    "stable": true,
    "output": "<path of the redacted PDF under <dir>/outputs/>",
    "rectangles": [[1, 72.0, 700.0, 180.0, 714.0]],
    "strings": ["Jane Doe"]
  },
  "write": {
    "status": "ok",
    "code": null,
    "output": "outputs/qpdf-0001.pdf",
    "sha256": "<hex of the output>",
    "bytes": 11873,
    "pages": 3,
    "stripped": ["ch_annotation", "ch_unreferenced"],
    "missing_references": 0,
    "reopen": {"status": "ok", "code": null, "pages": 3, "repairs": []}
  },
  "select": {
    "status": "ok",
    "code": null,
    "pages": [3, 1],
    "output": "outputs/qpdf-0001.select.pdf",
    "sha256": "<hex of the output>",
    "bytes": 7311,
    "reopen": {"status": "ok", "code": null, "pages": 2, "repairs": []}
  }
}
```

`write` is the identity write: the file opened, an empty edit plan written
with `pdfkit_write` to `<dir>/outputs/<id>.pdf`, and the output opened again
with `pdfkit_open` (`reopen`). `stripped` lists the channel rows whose count in
the write report is above zero, in the order of the channel table.
`select` keeps a seeded random proper subset of the pages, in a shuffled order,
and writes it to `<dir>/outputs/<id>.select.pdf`; `pages` lists the kept
source pages in output order, so output page *i* is source page `pages[i-1]`.
The subset is a function of the id alone: a SplitMix64 generator seeded with
the first eight bytes (big-endian) of the SHA-256 of `7:<id>` draws one number
per page in order and keeps the page when the number's top bit is set; an empty
draw keeps page 1 and a full draw drops the last page; the kept pages are then
shuffled by Fisher-Yates from the last index down, each swap index the next
number modulo the remaining count. `select` is `null` for a file of one page.
Neither runs when `open` did not succeed (their `status` is `skipped`).

`redact.output`, `redact.rectangles` (page, then the device rectangle in points,
lower-left and upper-right) and `redact.strings` are what
`tests/pdfium/compare.py --render` and `--leak` read; the redaction phases fill
them, and `stable` is the verb's own verdict, which `compare.py --render`
checks independently.

### 2.1 The status of a function

| `status` | Meaning | `code` |
|---|---|---|
| `ok` | the function ran and returned its result | `null` |
| `refused` | the input was refused (only `open` refuses) | the refusal code of `docs/DESIGN.md` chapter 19 |
| `not_implemented` | the function returned `PDFKIT_E_NOT_IMPLEMENTED` | `PDFKIT_E_NOT_IMPLEMENTED` |
| `error` | any other non-success status | the ABI status name; a `message` field follows `code` |
| `skipped` | not run because `open` did not succeed | `null` |
| `missing` | the file was absent (per-manifest form only) | `null` |

### 2.2 The fields each expectation reads

| Manifest column | Outcome fields | Pass when |
|---|---|---|
| `expect_open` = `opens` | `open.status`, `open.repairs` | `ok` and `repairs` is empty |
| `expect_open` = `repaired` | the same | `ok` and `repairs` is not empty |
| `expect_open` = `refused:<code>` | `open.status`, `open.code` | `refused` and the code matches |
| `expect_text` = `agree` | `text.status`, `text.text` | `ok`; the agreement itself is measured by `compare.py` against PDFium |
| `expect_text` = `known_gap:<reason>` | `text.status` | `ok` (counted apart, never as agreement) |
| `expect_images` = `decoded` | `images.status`, `images.undecodable` | `ok` and `undecodable` is empty |
| `expect_images` = `partial:<reason>` | `images.status` | `ok` |
| `expect_redact` = `stable` | `redact.status`, `redact.stable` | `ok` and `stable` is true |
| `expect_metadata` = `agree` | `metadata.status`, `metadata.agree` | `ok` and `agree` is true |
| `expect_write` = `identical` | `write.status`, `write.reopen` | `ok`, and `reopen.status` `ok` with no repairs and the same page count; the pixel identity is measured by `compare.py --check write` |
| `expect_write` = `known_gap:<reason>` | `write.status` | `ok` (counted apart) |
| `expect_select` = `ok` | `select.status`, `select.reopen`, `select.pages` | `ok`, and `reopen.status` `ok` with no repairs and as many pages as `select.pages` names |
| `expect_select` = `known_gap:<reason>` | `select.status` | `ok` (counted apart) |
| any column = `n/a` | none | not checked |

A function whose status is `not_implemented` makes its column **skipped** for
that file, never passed. `text.text` joins the pages' texts with a form feed
(U+000C), in page order.
