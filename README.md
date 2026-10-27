# pdfkit

pdfkit is a stand-alone C++ library and command-line tool that reads PDF files
into machine-readable text, images and metadata, and redacts text, images and
metadata into a **new** PDF. The original file is never changed. Version 0.1.0,
C ABI version 1.

- **Every glyph is text.** Invisible, clipped, off-page, white and duplicated
  text is extracted and flagged, because any extractor can reach it. A glyph
  pdfkit cannot decode is counted and named, never dropped in silence.
- **Redaction is surgery, not a box.** Matched glyphs are removed from the
  content stream. A placeholder such as `[NAME_1]` is drawn in a font pdfkit
  embeds, and every other glyph stays where it was. Images are boxed, blurred
  into a mosaic or removed in their samples, not covered.
- **The output is a fresh file.** It holds only reachable objects, with no
  earlier revisions, thumbnails, attachments, annotations, form data, bookmarks,
  JavaScript or structure-tree text, and with the Info dictionary and the XMP
  packet rewritten where asked and their history and identifiers regenerated.
  pdfkit re-reads every output before handing it back and refuses it if a
  redacted string, or the original value of a rewritten metadata field,
  survives anywhere.
- **Local only.** pdfkit opens no network connection, reads no path it was not
  given, runs no script, renders no page and performs no OCR.

## What it does

The catalogue is `CLAUDE.md` §2; `docs/ABI.md` documents each symbol and its
JSON, and `docs/DESIGN.md` the format rules behind it.

| # | Function | C ABI | CLI |
|---|---|---|---|
| 1.1 | Open, repairing damaged files and decrypting with an empty user password | `pdfkit_open` | every verb |
| 1.2 | Inspect: version, pages, boxes, encryption, repairs, warnings | `pdfkit_inspect` | `pdfkit inspect` |
| 2 | Extract text: blocks, lines and words in reading order, and every glyph with its font, position, decoding source and hidden-text classes | `pdfkit_text` | `pdfkit text` |
| 3.1 | List images: identity, encoding, size, colour space, masks and every draw | `pdfkit_images` | `pdfkit images` |
| 3.2 | Image pixels: PNG for a lossless source, the original JPEG for a DCT source | `pdfkit_image_data` | `pdfkit images --out` |
| 4 | Extract metadata: the Info dictionary and the XMP packet side by side | `pdfkit_metadata` | `pdfkit metadata` |
| 5 | Redact text by glyph range or search string, drawing a placeholder | `pdfkit_redact_text` | `pdfkit redact` |
| 6 | Redact images: box (with a caption), region boxes, blur (a destructive mosaic) or remove | `pdfkit_redact_images` | `pdfkit redact` |
| 7 | Rewrite metadata in both the Info dictionary and the XMP packet | `pdfkit_set_metadata` | `pdfkit redact` |
| 8 | Strip hidden channels (applied to every output) and report what is present | `pdfkit_channels` | `pdfkit channels` |
| 9 | Select pages, leaving out every object only a removed page reached | `pdfkit_select_pages` | `pdfkit select-pages`, `pdfkit redact` |
| 10.1 | Write a fresh file and self-check it | `pdfkit_write` | `pdfkit redact`, `pdfkit select-pages` |
| 10.2 | Check any file for surviving strings | `pdfkit_check` | `pdfkit check` |

The lifecycle symbols are `pdfkit_abi_version`, `pdfkit_version` (the library
and ABI versions, the build, and each catalogue function's status),
`pdfkit_close`, `pdfkit_edit_new`, `pdfkit_edit_free` and `pdfkit_buffer_free`.

## Using the command-line tool

Every verb writes one JSON document on standard output.

```
pdfkit inspect report.pdf
pdfkit text report.pdf --pages 1-3 --glyphs
pdfkit images report.pdf --out images/
pdfkit metadata report.pdf
pdfkit check redacted.pdf --strings "{\"strings\": [{\"text\": \"Jane Doe\"}]}"
pdfkit redact job.json
```

A redaction is a job file naming the input, the output and one section per
function, each handed byte for byte to it (`docs/ABI.md` §6):

```json
{
  "input": "report.pdf",
  "output": "report.redacted.pdf",
  "text": {"matches": [{"search": "Jane Doe", "placeholder": "[NAME_1]"}]},
  "images": {"treatments": [{"identity": "img:0123456789abcdef", "treatment": "box",
                             "caption": "[LOGO]"}]},
  "metadata": {"Author": "[AUTHOR]", "Title": null},
  "pages": {"pages": [1, 3]}
}
```

Exit codes: 0 success; 1 a usage error; 2 the input was refused (the JSON says
why and how to fix it); 3 the self-check refused the output, which is then not
written; 4 an internal error.

## Calling the library

Every function returns a `pdfkit_status` and writes its JSON answer, or on
failure a JSON error object with `code`, `failed`, `expected` and `fix`, into a
`pdfkit_buffer` the caller frees with `pdfkit_buffer_free`. No exception crosses
the ABI, and a document handle is used by one thread at a time.

From .NET (the ABI tier, `tests/abi/dotnet`, declares every symbol this way):

```csharp
[StructLayout(LayoutKind.Sequential)]
struct PdfkitBuffer { public IntPtr Data; public nuint Size; public IntPtr Reserved; }

static unsafe partial class Native
{
    [LibraryImport("pdfkit", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int pdfkit_open(byte* bytes, nuint size, string? options,
                                            out IntPtr document, ref PdfkitBuffer output);
    [LibraryImport("pdfkit", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int pdfkit_text(IntPtr document, string? options, ref PdfkitBuffer output);
    [LibraryImport("pdfkit")]
    internal static partial void pdfkit_buffer_free(ref PdfkitBuffer buffer);
    [LibraryImport("pdfkit")]
    internal static partial void pdfkit_close(IntPtr document);
}
```

From Python (`tests/abi/python/test_abi.py` declares every symbol):

```python
import ctypes, json

class Buffer(ctypes.Structure):
    _fields_ = [("data", ctypes.POINTER(ctypes.c_uint8)), ("size", ctypes.c_size_t),
                ("reserved", ctypes.c_void_p)]

pdfkit = ctypes.CDLL("pdfkit.dll")
pdfkit.pdfkit_open.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p,
                               ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(Buffer)]
pdfkit.pdfkit_text.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(Buffer)]
pdfkit.pdfkit_buffer_free.argtypes = [ctypes.POINTER(Buffer)]
pdfkit.pdfkit_close.argtypes = [ctypes.c_void_p]

data = open("report.pdf", "rb").read()
document, out = ctypes.c_void_p(), Buffer()
status = pdfkit.pdfkit_open(data, len(data), None, ctypes.byref(document), ctypes.byref(out))
pdfkit.pdfkit_buffer_free(ctypes.byref(out))
pdfkit.pdfkit_text(document, b'{"pages": [1]}', ctypes.byref(out))
text = json.loads(ctypes.string_at(out.data, out.size))
pdfkit.pdfkit_buffer_free(ctypes.byref(out))
pdfkit.pdfkit_close(document)
```

From Go, the same calls go through cgo against `pdfkit.h` and `pdfkit.lib`.

## Building

pdfkit is built on Windows x64 with the toolchain pinned in
`build/toolchain.md`: WinLibs MinGW-w64 GCC 16.1.0 (UCRT, POSIX threads, SEH),
CMake 4.3.2 and Ninja 1.13.2 on `PATH`, Python 3.12 for the checks and the
dataset tools, clang-format and clang-tidy 23.1.3 from PyPI
(`python -m pip install --user --require-hashes -r build/requirements-dev.txt`),
and the .NET 10 SDK for the ABI tier.

```
cmake --preset mingw-debug && cmake --build --preset mingw-debug
ctest --preset unit
cmake --preset mingw-release && cmake --build --preset mingw-release
python tests/abi/run.py
python build/check.py            # routine: debug build, clang-tidy of what changed
python build/check.py --full     # the merge gate: both builds, clang-tidy of everything
python build/package.py
```

Every preset compiles through the ccache that ships with the pinned toolchain,
with one cache in the main checkout's `build/cache/ccache/`, so a second
worktree, or a rebuild after switching branches, takes its objects from the
cache (`-DPDFKIT_COMPILER_CACHE=OFF` builds without it, to the same bytes).
`mingw-debug` keeps line tables only and links with lld from the cached
llvm-mingw; `mingw-release` links with GNU ld. `build/toolchain.md` section 6
holds what each setting buys, measured, and the settings rejected.

`python build/package.py` writes `dist/pdfkit-0.1.0-windows-x64.zip`: the DLL,
its import library, the header, the CLI, `LICENSE`, `NOTICE` and
`THIRD-PARTY-NOTICES.md`, each within its budget in `build/budgets.tsv`; with
`--verify` it packs twice around a clean rebuild and fails unless the two zips
are byte-identical. The DLL imports only operating-system libraries
(`python build/check_artefact.py`). The sanitizer builds are `mingw-sanitize`
(UBSan) and `clang-sanitize` (llvm-mingw, ASan and UBSan, at -O1), run by
`ctest --preset sanitize` and `ctest --preset clang-sanitize`; at a phase's end
the `clang-sanitize` build also reads every dataset file:
`python tests/dataset/run.py --sanitizer --runs 1 --timeout-scale 5`.

## How it is tested

Unit tests use fixtures written in code. On top of them, the library is
measured against a dataset of 960 public, generated, synthesised and mutated
PDF files (`dataset/`, the files held outside git), every function's
expectation written per file in `dataset/manifest.tsv`; each build phase's gate
is a number over that dataset, re-run together for the release
(`tests/dataset/gates/`). PDFium is the independent reader: text, image and
metadata agreement, pixel stability outside the redacted rectangles, and a
second leak check. PDFium is used in the test tier only and is never linked
into the library. Which gates hold, and the owner's open questions, are in
`OPEN-QUESTIONS.md`.

## Licence

Apache License 2.0; see `LICENSE` and `NOTICE`. Vendored components and the
embedded font subset carry their own permissive licences, listed in
`THIRD-PARTY-NOTICES.md`.
