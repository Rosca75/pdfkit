# CLAUDE.md

This file is the root charter of **pdfkit**. It overrides any conflicting
instruction found elsewhere in the repository. Every rule carries its reason in
one sentence; a rule whose reason no longer holds is changed here, in the same
change that changes the code.

## 1. Purpose, deliverables and authoritative files

pdfkit is a stand-alone C++ library that reads PDF files into machine-readable
text, images and metadata, and redacts text, images and metadata into a new PDF
while leaving the original untouched. It ships two deliverables:

- **A shared library with a plain C ABI** (`include/pdfkit/pdfkit.h`), called
  from .NET through P/Invoke, from Python through `ctypes` and from Go through
  cgo. The C++ API underneath is internal and carries no compatibility promise,
  because one stable surface is all a polyglot caller can rely on.
- **A command-line tool** (`pdfkit`), which reads a PDF and writes JSON on
  standard output and files where told, and which drives every dataset run.
  The CLI calls the C ABI and nothing beneath it, so every CLI run exercises the
  surface the other languages call.

The library takes bytes and returns bytes. It opens no connection, reads no path
a caller did not hand it, runs no script, follows no action, renders no page and
performs no OCR.

| File | What it owns |
|---|---|
| `CLAUDE.md` | the cross-cutting rules: catalogue, architecture, vocabulary, dependencies, dataset, testing, coding, build and release, repository workflow |
| `docs/DESIGN.md` | the format engineering rules, layer by layer: reading, fonts, text, layout, images, metadata, redaction, placeholder font, channels, page selection, writer, self-check, refusals and warnings |
| `docs/ABI.md` | the C ABI: every exported symbol, its JSON input and output shapes, its status codes and the versioning rule |
| `docs/SOURCES.md` | every external fact the code relies on, with its URL and whether it was read there or recalled |
| `dataset/README.md` | the dataset's manifest format, taxonomy, tools and licence rule |
| `dataset/taxonomy.tsv` | the issue tags and the minimum number of files per tag |
| `third_party/inventory.tsv` | every vendored component: path, version, upstream, licence, SHA-256 |
| `THIRD-PARTY-NOTICES.md` | the licence notices shipped with every artefact |
| `build/toolchain.md` | the pinned compiler, CMake, Ninja, clang-format and clang-tidy versions and the proxy behaviour |
| `build-5.md`, `build-6.md`, `build-10.md`, `build-11.md`, `build-12.md`, `build-13.md` | the build plans whose phase is not marked done (5 and 6 merged gate-failed; 10 is the release phase; 11 to 13 are the located check, find and hidden-layer removal, whose §0 is the contract doc-anonymiser is built against): each is authoritative for its phase's open measures until its phase is marked done, then moves to `docs/archive/build/` like the plans of phases 1 to 4 and 7 to 9 |
| `docs/change-01.md`, `docs/change-01a.md`, `docs/change-01b.md`, `docs/change-01c.md` | the reference panel order, awaiting the owner's launch: the parent holds its decisions and batch index, each batch file the work of one session |

Every other markdown file is a working document. `OPEN-QUESTIONS.md` holds the
questions waiting for the owner. A file added to or removed from this set moves
this table in the same change, because a charter that does not say which
documents are authoritative leaves each session to guess.

Markdown under `docs/archive/` is the record. No rule is derived from it and no
authoritative file links into it but the Build file column of §2, which says
where each function's plan and its deviations are kept: where a past decision
explains a current rule, the rule states the reason itself.

## 2. The function catalogue

The library does what this table lists and nothing else. A function is added by
adding a row here and following §3.6; it is never added as a side effect of
another function. `build/check_catalogue.py` holds this table, the header, the
registry in `src/api/catalogue.cpp` and the CLI verbs to one another.

| # | Function | C ABI symbol | Inputs | Outputs | Fails when | Build file |
|---|---|---|---|---|---|---|
| 1.1 | Open | `pdfkit_open` | PDF bytes; options JSON (limits, page ceiling) | a document handle; an open report (repairs made, decryption used, warnings) | no `%PDF` header and no page object (`not_pdf`); a header and no page object (`no_page`); a non-empty user password, or a revision 5 or 6 `/Perms` that does not decrypt (`password_required`); a handler other than `/Standard` or a revision, version, crypt filter or method it does not define (`unsupported_security_handler`); a stream, the file or the object count past a limit (`limit_exceeded`); a page count past the ceiling (`page_ceiling`); an unknown or malformed option | `docs/archive/build/build-3.md` |
| 1.2 | Inspect | `pdfkit_inspect` | document handle | JSON: version, page count, per page the boxes and rotation, encryption, repairs, warnings | never on a valid handle | `docs/archive/build/build-3.md` |
| 2 | Extract text | `pdfkit_text` | document handle; options JSON (pages, blocks, glyph level, re-serialisation check) | JSON: per page the blocks, lines and words in reading order, and the glyph list with every field of the glyph record (§4); the document fingerprint a glyph range names | a page number out of range or listed twice; an unknown option; a limit met while reading a page (`limit_exceeded`) | `build-5.md` |
| 3.1 | List images | `pdfkit_images` | document handle; options JSON (pages) | JSON: every image XObject and inline image with its identity, encoding, size, colour space, masks and occurrences; a warning per image that cannot be decoded | a page number out of range or listed twice; an unknown option; a limit met while reading a page (`limit_exceeded`) | `build-6.md` |
| 3.2 | Image pixels | `pdfkit_image_data` | document handle; image identity; options JSON (mask or base, a DCT image's pixels as PNG) | PNG bytes for a lossless source (or a DCT source asked as PNG), the original JPEG bytes for a DCT source | an unknown identity; the mask of an image without one; an image that does not decode (`undecodable_image`, naming the encoding and the fault) | `build-6.md` |
| 4 | Extract metadata | `pdfkit_metadata` | document handle | JSON: one row per field with the Info value and the XMP value side by side and whether they agree; the packet's state, namespaces and history | never on a valid handle (a packet that does not scan is reported, with its warning) | `build-10.md` |
| 5 | Redact text | `pdfkit_redact_text` | edit handle; JSON matches as glyph ranges from an extraction or as search strings with the caller's folding rules, each with its placeholder | JSON per match: occurrences found, device rectangles, how the placeholder was fitted | a placeholder outside the placeholder alphabet; a glyph range from another document, on a page outside it, empty or past the page's glyphs; a match with both or neither of a search and a range; an unknown folding or key; a limit met while reading a page | `docs/archive/build/build-8.md` |
| 6 | Redact images | `pdfkit_redact_images` | edit handle; JSON treatments per image identity: box (rectangle, optional caption and colour), region boxes, blur, remove; rectangles in image pixels or in page space | JSON per treatment: the pixels painted, the occurrences reached and their device rectangles, the original's encoded hash | an unknown identity; a second box, blur or remove of one identity; box, region or blur on an image that cannot be decoded (`undecodable_image`, remove is offered); a caption outside the alphabet; a caption or colour on blur or remove; a rectangle that covers no pixel; a blur of a bitonal image under 128 pixels | `docs/archive/build/build-9.md` |
| 7 | Rewrite metadata | `pdfkit_set_metadata` | edit handle; JSON field values, `null` removing a field | JSON: the fields as they will be written and the XMP properties each touches | a field outside the metadata field list; a value that is neither a string nor `null` | `build-10.md` |
| 8 | Strip hidden channels | `pdfkit_channels` (report); applied by every `pdfkit_write` | document handle | JSON: per channel of the channel table, present or not and the count, and for optional content the groups turned off, whose content every write removes | never on a valid handle | `docs/archive/build/build-7.md` |
| 9 | Select pages | `pdfkit_select_pages` | edit handle; JSON page list | the pages recorded; nothing is written until `pdfkit_write` | an empty, duplicated or out-of-range page list | `docs/archive/build/build-7.md` |
| 10.1 | Write | `pdfkit_write` | edit handle | PDF bytes; a write report (pages, size and hash, objects, channels stripped, rectangles changed, image treatments, self-check result, warnings) | the self-check finds a redacted string or an original value of a rewritten metadata field in any text surface (the refusal names the page and the surface); a limit met while a stream is read | `docs/archive/build/build-7.md`, self-check `docs/archive/build/build-8.md` |
| 10.2 | Check | `pdfkit_check` | PDF bytes; JSON strings with folding rules | JSON: every surviving occurrence by page and surface | the bytes do not open | `docs/archive/build/build-8.md` |

The lifecycle symbols carry no row of their own: `pdfkit_abi_version`,
`pdfkit_version` (the library and ABI versions, the build and each catalogue
function's implementation status), `pdfkit_close`, `pdfkit_edit_new`, `pdfkit_edit_free` and `pdfkit_buffer_free`.

The CLI exposes the catalogue as verbs, each with a stable exit code and JSON on
standard output:

| Verb | Calls | Writes files |
|---|---|---|
| `inspect` | 1.1, 1.2 | none |
| `text` | 1.1, 2 | none |
| `images` | 1.1, 3.1, 3.2 | one PNG or JPEG per image (and per mask) into `--out` |
| `metadata` | 1.1, 4 | none |
| `redact` | 1.1, 5, 6, 7, 9, 10.1, driven by a JSON job file | the output PDF named by the job |
| `select-pages` | 1.1, 9, 10.1 | the output PDF |
| `check` | 10.2 | none |
| `channels` | 1.1, 8 | none |
| `dataset` | every function the dataset manifest expects, per file | one JSON outcome per file into the run's report folder |

Exit codes: 0 success; 1 usage error; 2 the input was refused (the JSON names
why); 3 the self-check refused the output; 4 an internal error, which is always a
defect to fix, or `PDFKIT_E_NOT_IMPLEMENTED`, which names the function and is
reserved for a build that leaves a catalogue row out (this one implements every
row, and `pdfkit_version` says so per row). `docs/ABI.md` owns the JSON shapes.

**Content outside page content streams is dropped**: form fields, annotations,
bookmarks, named destinations, attachments, JavaScript and the structure tree's
text are neither extracted nor written, and every file the library writes is
stripped of them, because each is a channel a redaction can leak through and
none is in the catalogue. The Info dictionary and the XMP packet are the one
exception: they are extracted and rewritten, because they name people.

## 3. Architecture

### 3.1 The layers

The library is a stack of layers, each a folder of `src/` and a namespace of the
same name under `pdfkit::`. A layer includes only the layers listed beside it,
because a dependency tangle is how a parser change breaks the writer in silence.
`build/check_layout.py` reads every `#include` and fails on one that crosses a
line this table does not allow.

| Layer | Folder | Holds | May include |
|---|---|---|---|
| platform | `src/platform/` | the only compiler- and OS-specific code (export macros, endianness, checked arithmetic), the byte span and buffer types, the decoding budget, the error type, the hashes and ciphers (SHA-256/384/512, MD5, AES, RC4), the JSON value, writer and reader, the build description `pdfkit_version` reports, the Unicode tables and folding | the C++ standard library |
| syntax | `src/syntax/` | the lexer, the object model, the cross-reference reader, reconstruction, object streams, the standard security handler, the non-image filters, the page tree and resources, the content lexer, the Info dictionary and XMP packet reader | platform, zlib |
| fonts | `src/fonts/` | the font loader, simple and composite fonts, CMaps, encodings, the glyph list, the font-program readers (TrueType, CFF, Type 1), the Core 14 widths, the decoding ladder | syntax, platform |
| text | `src/text/` | the content interpreter (the graphics and text state machine), the glyph record, figure and image occurrences, the layout (words, lines, blocks, reading order), the hidden-text classes | fonts, syntax, platform |
| images | `src/images/` | image identity, the colour spaces, the masks, the codec adapters (DCT, JPEG 2000) and the written codecs (JBIG2, CCITT), PNG writing, the pixel treatments (box with its caption, region boxes, the blur mosaic, the soft-mask follow-up) and re-encoding | text, syntax, platform, libjpeg-turbo, OpenJPEG, zlib |
| writer | `src/writer/` | the reachable-object sweep, deterministic renumbering, the serialiser and cross-reference stream, the channel table, page selection with resource pruning, the metadata writer, the placeholder font embedding | syntax, platform, zlib |
| redact | `src/redact/` | match finding (glyph ranges and folded search), the content-stream surgery, the placeholder fitting, image treatments applied to the document, the edit plan, the self-check | writer, images, text, fonts, syntax, platform |
| api | `src/api/` | the C ABI implementation: handles, the catalogue registry, one file per catalogue function, JSON in and out, the catch-all that turns every failure into a status | every layer above |

The CLI (`cli/`) includes `include/pdfkit/pdfkit.h` and nothing under `src/`.
Tests (`tests/`) may include any layer.

### 3.2 The data flow

Bytes become objects (syntax), objects become fonts (fonts), a page's content
streams become glyph records, figures and image occurrences through one
interpreter (text), glyph records become words, lines and blocks (text), image
occurrences become decoded pixels (images). An edit plan (redact) holds the
caller's matches, treatments, page selection and metadata; the writer applies
them in a fixed order (page selection, text surgery, image treatments, metadata,
channel stripping, serialisation) and the self-check re-reads the result. One
interpreter serves extraction and redaction, because two readers of one stream
disagree, and a redaction that edits bytes a second reader located is a
redaction of the wrong bytes.

### 3.3 The C ABI over the C++ core

- Every exported function returns a `pdfkit_status` and writes its result, or on
  failure a JSON error object (`code`, `failed`, `expected`, `fix`), into a
  `pdfkit_buffer` the caller frees with `pdfkit_buffer_free`, because a message
  kept in global or thread-local state is global mutable state.
- No C++ exception crosses the ABI: every exported body is one call into
  `api::guarded`, which catches everything and maps it to a status, because an
  exception unwinding into P/Invoke or `ctypes` terminates the caller's process.
- Strings are UTF-8, sizes are `size_t`, handles are opaque pointers, and the
  calling convention is the platform's C default.
- The ABI only grows: a symbol never changes its signature or meaning, a JSON
  output only gains fields, and `pdfkit_abi_version`, 1 for the first release,
  is raised on every addition after it, because a caller binds to symbols once
  and is not rebuilt with us (nothing was released for a caller to bind to
  while the catalogue was built).
- A document handle is used by one thread at a time; distinct handles are
  independent. The library holds no global mutable state, so this is the whole
  threading contract.

### 3.4 The platform layer

Only `src/platform/` contains `#ifdef _WIN32`, compiler intrinsics, export
attributes or OS headers, because one thin layer is what keeps the rest
portable. `build/check_layout.py` fails on an OS header or a platform macro
anywhere else.

### 3.5 Repository layout

```
include/pdfkit/   the public C ABI header, and nothing else
src/<layer>/      one folder per layer of §3.1
cli/              the command-line tool, one file per verb under cli/verbs/
third_party/      vendored sources, one folder per component with its licence file
tests/unit/       unit tests over fixtures written in code (tests/unit/support/ holds the PDF builder)
tests/dataset/    the dataset harness: runs the CLI over the manifest and checks expectations
tests/pdfium/     the PDFium comparison: binding, text, image and metadata agreement, rendering, leak check
tests/abi/        the C ABI from .NET (dotnet/) and Python (python/), run by run.py
dataset/          manifest.tsv, taxonomy.tsv, README.md, tools/; files/, cache/ and reports/ are ignored
docs/             DESIGN.md, ABI.md, SOURCES.md, archive/build/ (the build plans of finished phases)
build/            check scripts and their tests, hooks, toolchain pin, presets helpers, fonts/ and unicode/ generators, size budgets, packaging
.github/          workflows that run the same checks
```

A file lives in the folder of its layer, its namespace is `pdfkit::<layer>`
(sub-folders add a nested namespace), and it holds one class named after the
file (`syntax/cross_reference.hpp` holds `pdfkit::syntax::CrossReference`).
`build/check_layout.py` fails on a file in the wrong folder, a namespace that
does not match its folder, or a header declaring two public classes.

### 3.6 Adding a catalogue function

A new function is one row in §2, one symbol in the header, one row in
`src/api/catalogue.cpp`, one file `src/api/<function>.cpp`, one verb file under
`cli/verbs/` when the CLI exposes it, its JSON shapes in `docs/ABI.md`, its
dataset expectation column in `dataset/manifest.tsv` and its gate in the build
file that delivers it. The registry and these files are what make the catalogue
growable without touching existing functions, and `build/check_catalogue.py`
fails until all of them agree.

## 4. Vocabulary

| Term | Meaning |
|---|---|
| glyph record | one shown glyph: page, stream (page contents or a form by object number, with the form path), operator index, operand index, byte range of the code in the string operand, font, code, Unicode text, decoding rung, advance, device-space box, render mode, fill colour, marked-content and optional-content context, clip, hidden classes |
| decoding rung | which step of the ladder answered a code: `to_unicode`, `glyph_name`, `font_program`, `actual_text`, `unreadable` |
| unreadable code | a code no rung answers; counted per page with the font's name and emitted as no character |
| hidden text | a glyph a viewer does not show but an extractor reaches; its classes are `render_invisible` (mode 3 or 7), `white_fill` (white with nothing drawn under it), `clipped`, `off_media`, `off_crop`, `optional_content_off`, `duplicate`, `covered` (under an image or a filled path drawn later) |
| figure | a group of path-painting operators with its device box |
| image identity | `img:` plus the first 16 hex digits of the SHA-256 over the encoded bytes and the decisive dictionary entries (`/Filter`, `/DecodeParms`, `/ColorSpace`, `/BitsPerComponent`, `/Decode`, `/SMask`, `/Mask`, `/Width`, `/Height`, `/ImageMask`) |
| occurrence | one draw of an image: page number and ordinal in that page's draw order, forms included; never a byte offset |
| match | a range of glyph records the caller wants redacted, given as glyph ranges or found by a folded search; continuous across operators, lines and hyphens |
| placeholder | the string drawn where a match was, in the placeholder font, from the placeholder alphabet `[`, `]`, `_`, `0`-`9`, `A`-`Z` |
| placeholder font | a CID-keyed TrueType subset of Liberation Sans (Regular and Bold) holding the placeholder alphabet and `.notdef`, embedded once per output file with a `ToUnicode` map |
| treatment | what is done to an image identity: `keep`, `box`, `blur`, `remove`, and `region` boxes over rectangles the caller found in its pixels |
| channel | a place in a PDF where text or pixels can survive outside the page's drawn content; one row of the channel table |
| edit plan | the caller's matches, treatments, page selection and metadata values held by an edit handle until `pdfkit_write` |
| self-check | the re-read of an output with the library's own reader, refusing the output when a redacted string survives in any text surface |
| dataset | the 500 to 1,000 PDF files listed in `dataset/manifest.tsv`, held outside git |
| gate | a number measured over the dataset that a build phase must reach |
| independent reader | PDFium, used in `tests/pdfium/` only |

## 5. Why pdfkit is built this way

These are the reasons behind the rules of this charter and of `docs/DESIGN.md`.
A session that wants to change a rule reads its reason here first.

- **pdfkit is a literal tool; its caller owns the business rules.** pdfkit is
  driven by doc-anonymiser-for-.NET, which decides the values and spelling
  variants to send, word boundaries and which short words may go. A value (a
  search string) is removed wherever it occurs, inside longer words too, and
  verified everywhere; an expression at a location (a glyph range) is removed
  there alone and verified there alone, because a caller redacting one "EU"
  must not lose every "eu"; a rewritten metadata field is verified in the
  metadata. Two copies of a business rule drift, and a redaction library that
  second-guesses its caller leaks what the caller asked to remove or removes
  what it asked to keep.
- **A regenerated document is not the user's document.** A same-format export
  rebuilt from extracted text loses what the user sees. pdfkit edits the
  original's content streams and copies every untouched object byte for byte.
- **The placeholder never comes from the document's font.** Subset fonts
  (`ABCDEF+Name`) routinely lack `[`, `]`, `_` and digits. pdfkit embeds its own
  subset of an OFL font and draws every placeholder with it.
- **A thin extractor is worse than none.** A silent extraction failure reads as
  "nothing to redact". pdfkit owns every glyph down to the operator bytes that
  drew it, counts and names what it cannot read, and reads hidden, clipped and
  off-page text, because an extractor reaches it.
- **No free-licence library edits PDF text in place with fidelity**, measured on
  2026-10-24: PDFium's page regeneration drops CMYK and ICC colours, shadings,
  soft masks and Type 3 text and has no setter for metadata, bookmarks or the tag
  tree; PdfPig reads well and writes only new files; PDFsharp is AGPL after
  6.2.4; QPDF knows nothing of content streams; pdf.js saves incrementally. That
  is why pdfkit is written, and why PDFium is its test-tier reference and
  nothing more.
- **The technique is known.** MuPDF's redaction filter (read in source, AGPL,
  technique only, no code taken) removes glyphs per code, keeps the advance and
  closes the hole with a `TJ` adjustment or a fresh text matrix; UniPDF closes it
  with a width adjustment; every vendor draws overlay text in its own font sized
  to the hole.
- **Redaction fails on the channels nobody listed.** The public record (the NSA
  guidance, the TSA manual, the Manafort filing redacted with boxes over live
  text, the 2025 to 2026 DOJ releases) is a record of incremental updates,
  unreferenced objects, thumbnails, metadata and attachments. pdfkit writes a
  fresh file holding only reachable objects, keeps the channel list as a table
  with a planted-marker test per row, and re-reads its own output before handing
  it back.
- **Leaving the following glyphs in place leaks the removed span's width**
  (Bland et al., PETS 2023, recovered redacted names from glyph positions in
  eleven tools). The owner accepts the leak for layout fidelity; re-anchoring the
  rest of the line would be an opt-in switch and is never the default.
- **Extractors disagree on what is text**, and the specification itself says
  TrueType encoding heuristics are "not described here". The decoding ladder
  records which rung answered each code, and the reading gate is measured
  agreement with an independent reader over a large dataset, never a count of
  unit tests.
- **Codecs are the one place a dependency beats a rewrite.** JBIG2 and JPEG 2000
  decoders are thousands of lines with a history of bugs, so permissive codec
  libraries are vendored, and only the codecs with no permissive C++ library
  (JBIG2, CCITT) are written here.
- **Determinism is load-bearing.** A reference implementation's word extractor
  had a confirmed non-deterministic order; every pdfkit sort is stable and every
  geometric key is rounded, so two runs over one file produce the same bytes.

`docs/SOURCES.md` holds the URL behind each of these.

## 6. Dependencies and licences

- **Every dependency carries a licence permitting free commercial use**: MIT,
  BSD, Apache 2.0, zlib, the permissive data licences (Unicode License v3, the
  Adobe Glyph List's BSD-3, the Core 14 AFM notice) and the SIL OFL 1.1 for
  fonts. GPL, LGPL, AGPL and dual commercial licences are disqualified by
  name: MuPDF, Ghostscript, Poppler, iText, PdfClown and jbig2dec among them,
  because the library is meant to be embedded in commercial products.
- **Dependencies are vendored as source** under `third_party/<name>/` with the
  upstream licence file, listed in `third_party/inventory.tsv` with version,
  upstream URL, licence and SHA-256 of the upstream archive, because a build
  that fetches is a build that changes under us. `build/check_third_party.py`
  recomputes each hash and fails on a component missing from the inventory, a
  licence file missing, or a licence outside the list above.
- **Only what is not light enough to write is vendored**:

| Component | Licence | Why it is vendored |
|---|---|---|
| zlib | zlib | Flate is used by every PDF and its inflate and deflate are mature |
| libjpeg-turbo | BSD-3 / IJG / zlib | DCT decoding and the re-encoding of a treated JPEG |
| OpenJPEG | BSD-2 | JPEG 2000 decoding, thousands of lines with a bug history |
| doctest | MIT | the unit-test framework, test builds only |
| Liberation Sans (Regular, Bold) | SIL OFL 1.1 | the source of the placeholder font subset, cut at build time |
| Adobe Glyph List | BSD-3 | glyph-name to Unicode data |
| Core 14 AFM widths | Adobe's AFM notice | widths of the standard fonts when a file embeds none |
| Unicode data (case folding, decomposition) | Unicode License v3 | the folding rules a search applies |

- **Written here**, ported only from permissively licensed references where a
  reference is used and naming it in the file's header: the parser, the security
  handler and its ciphers, the non-image filters, the fonts, the decoding
  ladder, the layout, the writer, the TrueType subsetter and the caption bitmap
  font tool (both under `build/fonts/`, both cutting from Liberation Sans), the
  PNG writer, the JSON
  reader and writer, the JBIG2 decoder (from pdf.js, Apache 2.0) and the CCITT
  decoder (from pdf.js, Apache 2.0, or PDFBox's TwelveMonkeys code, BSD-3).
- **PDFium is test-only.** bblanchon's prebuilt binary (BSD-3 and Apache 2.0,
  packaging MIT) is fetched by `tests/pdfium/fetch.py`, pinned by hash, into the
  shared cache (§8.4), and is never linked into the library or shipped, because
  an independent reader that the library depends on is not independent.
- **Reference readers judge as a panel, test-only.** A reading choice or a
  gate is judged by the majority of PDFium, Poppler's `pdftotext`, MuPDF's
  `mutool`, pdf_oxide and pdf-extract (QPDF and lopdf for structure), because
  one reader's quirks counted as truth make pdfkit's misses and its
  reference's indistinguishable. Each is fetched by hash into the shared cache
  and run as a separate process by `tests/`; none is linked, vendored or
  shipped, so GPL and AGPL readers are allowed there and only there, as the
  compiler and ccache are.
- **Every source file carries the licence header**: the first lines are
  `// SPDX-License-Identifier: Apache-2.0` and
  `// Copyright 2026 The pdfkit authors` (with `#` in Python, CMake and
  PowerShell), then the purpose header. `build/check_headers.py` fails on a file
  under `include/`, `src/`, `cli/`, `tests/`, `build/` or `dataset/tools/`
  without them. Vendored files keep their own headers.

## 7. The dataset programme

### 7.1 What the dataset is

A dataset of at least 500 and at most 1,000 PDF files is the regression suite,
the acceptance gate and the catalogue of what breaks. `dataset/manifest.tsv` is
committed; the files are not. `dataset/README.md` owns the column definitions.
One row per file: `id`, `set`, `source` (URL or generator), `licence`, `sha256`,
`bytes`, `pages`, `producer`, `tags`, an expected outcome per function the
dataset verb measures (`expect_open`: `opens`, `repaired` or `refused:<code>`;
`expect_text`, `expect_fonts`, `expect_images`, `expect_metadata`,
`expect_write`, `expect_select`, `expect_redact_text`, `expect_set_metadata`,
`expect_redact_images`, each a pass value, `known_gap:<reason>` or `n/a`), and
`notes`. The manifest holds 960 rows.

### 7.2 Composition and licences

- **No client document is ever in the dataset.** Files come from public test
  corpora, from generators run on the owner's laptop, and from synthesis and
  mutation, because the dataset must be shareable and reproducible.
- **Public corpora** are scouted on the web and admitted when their licence or
  terms allow local testing; the licence is recorded per file. The named sets:
  qpdf's test files, PDFium's test resources and corpus, the PDF Association's
  safedocs and pdf20examples, the Open Preservation Foundation format corpus,
  pdf.js's test files, PdfPig's and PDFBox's test files, py-pdf's sample files,
  the veraPDF corpus, one 1,000-file zip of Digital Corpora's
  CC-MAIN-2021-31-PDF-UNTRUNCATED as the wild sample, and, scouted beyond these,
  pdf-differences, pdfminer.six, pdfplumber, pikepdf, Apache Tika and NICAR
  2025. corkami's proofs of concept, the PDF Association's stressful corpus and
  Digital Corpora's UNSAFE-DOCS carry no licence that allows it and are not
  fetched (`dataset/sources.tsv` records each set's status).
- **No PDF is committed to git**, whatever its licence, because unit fixtures
  are written in code and the dataset lives outside git; `build/check_repo.py`
  fails on any committed file whose bytes start with `%PDF`.
- **Generated files** come from one reference document saved through every
  generator on the laptop (Word Save as PDF and PDF/A, Microsoft Print to PDF
  from Word, Excel and PowerPoint, Edge Save as PDF) by
  `dataset/tools/generate_office.ps1`, which kills any application that hangs or
  shows a dialog; Word and Edge cannot be driven unattended on the laptop, so
  their two tags wait on the owner (`OPEN-QUESTIONS.md`).
- **Synthesised files** fill every taxonomy tag no public file covers, written by
  `dataset/tools/synth/` from a description of the pattern known to break
  extraction, because a tag with no file is a blind spot.
- **Mutated files** come from clean ones through a seeded mutator
  (`dataset/tools/mutate.py`): cross-reference table removed, `startxref` wrong,
  `%%EOF` missing, junk before the header, `/Length` wrong, `endstream` and
  `endobj` missing, two trailers, an incremental update that changes a page's
  text, linearised with stale hints, object streams, and the encryption
  revisions with an empty user password. A mutated file either opens with the
  same text as its source or is refused naming the damage, never opens with
  different text in silence.

### 7.3 The taxonomy

`dataset/taxonomy.tsv` lists every tag with its group and the minimum number of
files that must carry it. The groups: structural damage (one tag per damage
class), encryption (per revision), fonts (per decoding path: ToUnicode correct,
missing, wrong; Differences with glyph-list names and with `uniXXXX`, `gNN`,
`Cdd` names; symbolic TrueType; Type 1 built-in encoding; CFF charset; identity
CID; predefined CMap; Type 3; text as outlines), text layout (no space glyphs,
kerning-split words, ligatures, hyphenation across lines and pages,
multi-column, rotated, vertical, right-to-left, tables, headers and footers),
hidden text (per class of §4), images (per encoding, per colour space, soft
mask, stencil, inline, shared across pages, in a form), hidden channels (per
row of the channel table), generators (per producer), redaction traps
(overlapped text, incremental updates, deniable removal), and metadata (Info
only, XMP only, both agreeing or disagreeing, UTF-16 strings, custom
namespaces, XMP history, a packet that does not scan).
`python dataset/tools/report.py` prints the count per tag and fails when a tag
is under its minimum.

### 7.4 Every phase measures itself on the dataset

A build phase's gate is a number over the dataset: files opened, files
repaired, agreement with PDFium, images decoded, self-checks passed, redactions
pixel-stable outside the changed rectangles. A count of unit tests is never a
gate, because unit tests prove what the author thought of and the dataset proves
what producers wrote. Each run writes its report to
`dataset/reports/<run-id>/` (ignored), and the summary goes into the merge
commit message of the phase.

### 7.5 PDFium, the independent reader

PDFium measures text agreement (the per-file bag-of-words F1, order-insensitive,
over words of two or more letters or digits, lower-cased under the default
folding), renders pages before and after a redaction to prove pixel stability
outside the reported rectangles, and is the second extractor of the leak check.
It is fetched by hash into the shared cache and loaded by `tests/pdfium/` through
`ctypes`, never by the library.

## 8. Testing

### 8.1 The tiers

| Tier | What it proves | Command |
|---|---|---|
| unit | each component against fixtures written in code by `tests/unit/support/` (no committed PDF) | `ctest --preset unit` |
| sanitizer | the unit tier under the sanitizers the pinned toolchain supports (`build/toolchain.md` lists them): UBSan in trap mode under GCC, ASan and UBSan under llvm-mingw; at a phase's end, the `clang-sanitize` build's dataset verb over every manifest file, with no sanitizer report | `ctest --preset sanitize`, `ctest --preset clang-sanitize`, `python tests/dataset/run.py --sanitizer --runs 1 --timeout-scale 5 --run-id <run-id>-asan` |
| dataset | the catalogue's expectations over every manifest file, twice for determinism, with a timeout of 30 s per started block of 200 pages and the refusal invariant; each phase's gate script judges a run | `python tests/dataset/run.py`, `python tests/dataset/gates/phase<n>.py --run <run-id>` |
| pdfium | text, image and metadata agreement, pixel stability and the second leak check against PDFium; what PDFium says of an input file is kept in `build/cache/pdfium-results/` of the main checkout, keyed by the file's SHA-256, the PDFium release and the request, because neither changes between runs, and every answer is computed in a library just reset, because PDFium carries font state from one document to the next | `tests/pdfium/text_agreement.py`, `image_agreement.py`, `metadata.py`, `compare.py --check write,select,text,redact`, `images.py`, `leak.py --metadata --images`, each with `--run <run-id>` |
| abi | the C ABI loaded from .NET and from Python | `python tests/abi/run.py` |
| checks | layout, headers, comments, catalogue, third party, no-network, repository, paths, generated files, warnings, formatting and tidy, the artefact's imports | `python build/check.py --full` (the merge gate), `python build/check.py` (routine), `--fast` (the pre-push hook) |

The checks tier has three modes, because clang-tidy over every source and a
second build are most of its time and most changes touch few files. A routine
run builds mingw-debug and runs clang-tidy only over the sources that reach a
file changed since the remote main branch, each source's findings cached under
a hash of everything that decides them; `--fast` does the same without the
build; `--full` builds mingw-debug and mingw-release, runs clang-tidy over
every source with its cache set aside and reads the artefacts' tables. `--full`
is the merge gate of every branch and phase, so a source a routine run leaves
out was analysed in full when its last change entered main.

### 8.2 Rules

- **Tests move with the code.** A change is finished when its unit tests, its
  dataset tags and expectations, and the gate numbers of its build file moved
  with it, because a test written later is written from the code rather than
  from the rule.
- **Fixtures are written in code.** `tests/unit/support/pdf_builder` writes
  objects, streams, cross-reference tables or streams, trailers, encryption and
  every damage the mutator knows, so a test reads as the file it makes.
- **A test written first fails first.** The failing test that defines a
  behaviour is written and seen red before the code.
- **The dataset is the gate, the unit tier is the floor.** A green unit tier
  with a regressed dataset number is a regression.
- **Tests run at planned points, not after every edit.** While coding, the
  unit tests of the component touched (`pdfkit_tests -tc=...` or a
  test-case filter); per batch of commits, the routine check and the unit
  tier; the dataset as a smoke set of about 60 files covering the phase's
  tags once mid-phase, the full gate once at the phase end and again only
  after a fix to one of its failures; the sanitizer tiers and
  `python build/check.py --full` at the merge. A full tier is never re-run
  "to be sure", because the full check and the gate run take 17 and 23
  minutes, and a long session spent waiting on repeats is a session not
  building.
- **A missing optional input skips by name.** A dataset set, PDFium or a
  generator that is absent makes its test skip with the command that supplies
  it, never pass in silence.

## 9. Coding rules

- **C++20, no compiler extensions** (`CMAKE_CXX_EXTENSIONS OFF`), because the
  library must build on every toolchain a consumer uses.
- **Warnings are errors under the pinned set** in `build/warnings.cmake`; a
  warning is fixed, never silenced, and a suppression is named line by line in
  `build/warnings.cmake` with its reason.
- **`clang-format` and `clang-tidy` configurations are committed** and run by
  `build/check.py`; a formatting difference fails the check.
- **No raw owning pointer and no global mutable state.** Ownership is a value,
  a `std::unique_ptr` or a container; a table that never changes is `constexpr`
  or a function-local `static const`, because global state breaks the
  one-handle-per-thread contract.
- **Every allocation is bounded.** A size read from the file (a `/Length`, a
  `/Size`, a width times a height, an INDEX count) is checked against the
  document's budget before anything is reserved, and decoding counts bytes
  produced against the inflate limits (512 MiB per stream, 2 GiB per document,
  both configurable at open), because a header is a claim and a few kilobytes of
  Flate can inflate to gigabytes.
- **No undefined behaviour is tolerated.** Arithmetic on file-supplied values
  goes through the checked helpers of `src/platform/`; the sanitizer tier runs on
  every phase's gate.
- **No exceptions across the C ABI** (§3.3).
- **No network, no paths, no scripts.** Nothing under `include/`, `src/` or
  `cli/` (beyond the CLI's own reading of the paths its user names) opens a
  socket, a URL or a file; nothing follows a `/URI`, a launch action, an external
  stream (`/F`) or runs JavaScript. `build/check_network.py` fails on a socket,
  HTTP or URL API and on a link to a networking library; the dataset fetch tools
  and `tests/pdfium/fetch.py` are its only named exemptions, each with its
  reason, because they are developer tools that never ship.
- **Determinism.** Every sort is stable; every geometric key is rounded to
  0.01 pt before comparison; no output depends on hash-map iteration order,
  time, locale or randomness; numbers are formatted with `std::to_chars`;
  identifiers are hashes of content. The dataset tier runs each file twice and
  compares the bytes.
- **Every file opens with a purpose header** after the licence header: what the
  file is for and why it exists, in a few sentences.
- **Comments state intent in the present tense and few lines go unexplained.**
  A comment describes the code as it is, names no phase, build file, ticket or
  past version, and never says "used to"; where a past decision explains code,
  the comment states the reason. A rule copied from a reader rather than from
  the specification names the reader, because that is the fact's status.
  `build/check_comments.py` fails on a phase or build-file citation and on delta
  words.
- **Error messages are actionable**: what failed, what was expected, how to fix
  it, at the C ABI (the three JSON fields) and in the CLI alike, because a
  refusal the caller cannot act on is a support call.
- **Every external fact carries its source.** A format rule, an algorithm or a
  licence term implemented from outside names in its comment where it comes from
  and has a row in `docs/SOURCES.md` saying whether it was read there or
  recalled; a recalled fact is tested against a generated specimen.

## 10. Build and release

- **CMake with Ninja**, driven by `CMakePresets.json`: `mingw-debug`,
  `mingw-release`, `mingw-sanitize` and `clang-sanitize` (llvm-mingw, for
  AddressSanitizer). Targets: `pdfkit` (the shared library), `pdfkit_core` (the
  static library every test links), `pdfkit_cli` (the `pdfkit` executable),
  `pdfkit_tests` (the unit tier), `pdfkit_image_census` (the image decoders'
  dataset census), and one target per vendored component.
- **The toolchain is pinned** in `build/toolchain.md`: WinLibs MinGW-w64 GCC
  16.1.0 (UCRT, POSIX threads, SEH), CMake 4.3.2, Ninja 1.13.2, and clang-format
  and clang-tidy from PyPI at the versions recorded there, with the proxy
  behaviour observed on the laptop. A version moves only by a change to that
  file and a full check run.
- **The shared library imports only operating-system DLLs**: libgcc, libstdc++
  and winpthread are linked statically, because a P/Invoke caller cannot be
  asked to ship a compiler runtime. `build/check_artefact.py` reads the import
  table and fails on any other import.
- **Artefacts per architecture**: `dist/pdfkit-<version>-windows-x64.zip`
  holding `pdfkit.dll`, its import library, `pdfkit.h`, `pdfkit.exe`,
  `LICENSE`, `NOTICE` and `THIRD-PARTY-NOTICES.md`, produced by
  `python build/package.py`; further architectures are added as the toolchain
  pin gains them. `build/budgets.tsv` holds the size budget of the DLL, the
  executable and the zip (the first release package plus 25%) and the
  packaging step fails past it; the zip's entries are sorted, dated 1980-01-01
  and the import library's member dates zeroed, so `python build/package.py
  --verify`, which packs twice around a clean rebuild, finds one set of bytes.
- **CI runs the same checks**: `.github/workflows/` call `build/check.py --full`,
  the unit tier and the abi tier; the hosted runners do not start for this
  repository, so the pre-push hook
  (`build/hooks/pre-push`, installed by `python build/install_hooks.py`) is the
  gate, and it runs `build/check.py --fast`, a mingw-debug build and the unit
  tier. The merge gate of a branch is `python build/check.py --full` with the
  unit, sanitizer and abi tiers on the merged tip (§8.1); a branch that changes
  only markdown and the paths check's allow-list `build/paths_pending.tsv`
  merges on `python build/check.py --fast`, because its merged tip builds the
  bytes `main` already proved and the fast run reads both.
- **Builds are cached and measured for speed** (`build/toolchain.md`, "Build
  speed", holds what each setting does, its measured gain and what to check
  when adding a file): every compile of every preset goes through the ccache
  of the pinned toolchain with one cache in the main checkout's
  `build/cache/ccache/`, so a worktree builds from the objects another
  compiled, because every worktree otherwise rebuilds every preset from
  scratch. A setting that changes the compile is kept only when it measurably
  pays and never changes what the shipped DLL does, because a faster build of
  other bytes is a different release. Each source compiles alone, with no
  unity build and no precompiled header, so a source names every header it
  uses and its file-local names stay its own; the settings tried and rejected
  are listed there with the numbers that rejected them.

## 11. Repository workflow

- **One branch per build phase**, named `phase/<n>-<slug>`, each in its own
  worktree, because two sessions sharing one checkout re-point each other's
  branch and stage each other's files.
- **Main is always green.** A phase merges into `main` only after its checks,
  unit tier and gate pass on the merged tip; the merge is made locally (no pull
  request) and pushed. The merge commit message carries the gate's numbers and
  the trailer `Build-Phase: <n> done`; a phase whose checks and leak measures
  pass but whose gate does not merges with `Build-Phase: <n> gate-failed` and
  the owner's question, and the branch carries `Build-Phase: <n> incomplete`
  with the reason when the session stops early.
- **Shared caches live in the main checkout**, resolved through
  `git rev-parse --git-common-dir`: the dataset files under `dataset/files/`,
  and under `build/cache/` the PDFium binary, the llvm-mingw toolchain, the
  compile cache (`build/cache/ccache/`), clang-tidy's findings
  (`build/cache/clang-tidy/`) and PDFium's answers about the input files
  (`build/cache/pdfium-results/`), so every worktree reads one copy and none
  repeats work another did. Each is keyed by everything that decides its
  content, so deleting one costs time and never changes a result.
- **A question for the owner** goes into `OPEN-QUESTIONS.md` with the
  recommended answer first and the price of each option in hours and model
  spend; work that does not depend on the answer continues.
- **A deviation from a build file** is recorded in that file's deviations
  section and in every downstream build file it affects, in the same change.

## 12. Where to look next

- The format rules each layer implements: `docs/DESIGN.md`.
- The exported symbols and their JSON: `docs/ABI.md`.
- The URL behind every external fact: `docs/SOURCES.md`.
- The dataset's columns, tags and tools: `dataset/README.md`, `dataset/taxonomy.tsv`.
- The pinned toolchain: `build/toolchain.md`.
- A phase's plan: `build-<n>.md` at the root while its phase is not marked
  done in `git log --grep "Build-Phase: <n> done"`, `docs/archive/build/`
  after; a phase marked `gate-failed` keeps its plan at the root, with its
  open measures and the owner's question.
- Questions waiting for the owner: `OPEN-QUESTIONS.md`.
