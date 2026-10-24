# pdfkit

pdfkit is a stand-alone C++ library and command-line tool that reads PDF files
into machine-readable text, images and metadata, and redacts text, images and
metadata into a **new** PDF. The original file is never changed.

- **Every glyph is text.** Invisible, clipped, off-page, white and duplicated
  text is extracted and flagged, because any extractor can reach it. A glyph
  pdfkit cannot decode is counted and named, never dropped in silence.
- **Redaction is surgery, not a box.** Matched glyphs are removed from the
  content stream. A placeholder such as `[NAME_1]` is drawn in a font pdfkit
  embeds, and every other glyph stays exactly where it was.
- **The output is a fresh file.** It holds only reachable objects, with no
  earlier revisions, thumbnails, attachments, annotations, form data, bookmarks,
  JavaScript or structure-tree text, and the Info and XMP metadata rewritten.
  pdfkit re-reads every output before handing it back and refuses it if a
  redacted string survives anywhere.
- **Local only.** pdfkit opens no network connection, reads no path it was not
  given, runs no script, renders no page and performs no OCR.

## What it does

| # | Function | C ABI | CLI |
|---|---|---|---|
| 1 | Open (with repair of damaged files and empty-password decryption) and inspect | `pdfkit_open`, `pdfkit_inspect` | `pdfkit inspect` |
| 2 | Extract text: reading order as blocks, lines and words, plus every glyph with its font, position, decoding source and hidden-text flags | `pdfkit_text` | `pdfkit text` |
| 3 | Extract images as PNG (lossless sources) or the original JPEG (DCT sources), with their placements | `pdfkit_images`, `pdfkit_image_data` | `pdfkit images` |
| 4 | Extract metadata: the Info dictionary and the XMP packet side by side | `pdfkit_metadata` | `pdfkit metadata` |
| 5 | Redact text by glyph range or by search string, drawing a placeholder | `pdfkit_redact_text` | `pdfkit redact` |
| 6 | Redact images: box, blur (a destructive mosaic) or remove | `pdfkit_redact_images` | `pdfkit redact` |
| 7 | Rewrite metadata in both the Info dictionary and the XMP packet | `pdfkit_set_metadata` | `pdfkit redact` |
| 8 | Strip hidden channels (applied to every output) and report what is present | `pdfkit_channels` | `pdfkit channels` |
| 9 | Select pages, with every object only removed pages reached left out | `pdfkit_select_pages` | `pdfkit select-pages` |
| 10 | Write a fresh file and self-check it; check any file for surviving strings | `pdfkit_write`, `pdfkit_check` | `pdfkit check` |

The CLI writes JSON to standard output. Every function is reachable from any
language that can call C: .NET through P/Invoke, Python through `ctypes`, and
Go through cgo. `docs/ABI.md` documents each symbol and its JSON.

## Building

pdfkit is built with CMake and Ninja. The pinned toolchain is recorded in
`build/toolchain.md` (WinLibs MinGW-w64 GCC on Windows x64).

```
cmake --preset mingw-release
cmake --build --preset mingw-release
ctest --preset unit
python build/check.py
```

The release package is produced with `python build/package.py`, which writes
`dist/pdfkit-<version>-windows-x64.zip`. The package contains the DLL, its
import library, the header, the CLI and the notices. The DLL depends only on
operating-system libraries.

## How it is tested

Unit tests use fixtures written in code. On top of them, the library is measured
against a dataset of 500 to 1,000 public, generated, synthesised and mutated PDF
files (`dataset/`). PDFium is the independent reader: text agreement, pixel
stability outside the redacted rectangles, and a second leak check. PDFium is
used in the test tier only and is never linked into the library.

## Licence

Apache License 2.0; see `LICENSE` and `NOTICE`. Vendored components and the
embedded font subset carry their own permissive licences, listed in
`THIRD-PARTY-NOTICES.md`.
