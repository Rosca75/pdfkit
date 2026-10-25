# build-6 — Image decoders, identity, occurrences and the `images` command

| Field | Value |
|---|---|
| Phase | 6 |
| Branch | `phase/6-images` |
| Depends on | 3 (decoders); 5 (the listing step only) |
| Parallel with | 4, 5, 7 |
| Model | `opus` |
| Owns | `src/images/`, `tests/unit/images/`, `third_party/libjpeg-turbo/`, `third_party/openjpeg/`, `src/api/images.cpp`, `src/api/image_data.cpp`, `cli/verbs/images.cpp`, `tests/pdfium/image_agreement.py`, `dataset/tools/synth/images.py` |
| Size | about 9,000 lines of C++ including tests, plus two vendored codec libraries; the largest phase, which may need two sessions |
| Price | estimate: 18 to 26 hours over one or two sessions, about $300 to $450 of model spend at list prices (the design record's stage 4 put the CCITT port at about 1,000 and the JBIG2 port at about 2,600 lines of C# for 15 to 25 hours and $250 to $450 with the pixel-box primitive; this phase adds colour spaces and the two codec adapters and drops the treatments, scaled by 1.3 for C++) |

Shared files touched additively: `third_party/inventory.tsv`,
`THIRD-PARTY-NOTICES.md`, `docs/SOURCES.md`, `docs/ABI.md` (the two image
sections), the `images` column of `dataset/manifest.tsv`, and the top-level
`third_party/CMakeLists.txt` (two `add_subdirectory` lines). The team lead
resolves conflicts in these with phases 4 and 7.

## 0. What this phase ships

- Catalogue row **3.1, List images** (`pdfkit_images`): every image XObject and
  inline image with its identity, encoding, size, colour space, bits per
  component, masks and occurrences (page and draw ordinal, through forms), and a
  warning per image that cannot be decoded.
- Catalogue row **3.2, Image pixels** (`pdfkit_image_data`): PNG bytes for a
  lossless source, the original JPEG bytes for a DCT source, the mask as a
  separate PNG on request.
- The decoders of `docs/DESIGN.md` §9: Flate and the other non-image filters
  (phase 3's) followed by the colour-space reader; DCT through libjpeg-turbo;
  JPEG 2000 through OpenJPEG; JBIG2 and CCITT written here.
- The CLI verb **`images`** (`pdfkit images <file> --out <dir> [--pages ...]`),
  writing one PNG or JPEG per image and per mask, and the `images` expectation
  of the `dataset` verb.
- The decoded-pixel primitive phase 9's treatments (box, blur, re-encode) build
  on; the treatments themselves are phase 9's.

## 1. What it reads from earlier phases

- `src/syntax/` as merged by phase 3: streams and their encoded bytes, the
  filter chain that stops at an image codec and hands back the encoded bytes with
  the remaining filter name, `/DecodeParms`, the inflate limits, the content
  lexer's inline images (`BI` dictionary, data range).
- `src/platform/`: the budget (a width times a height times the components is
  checked before a buffer is reserved), checked arithmetic, SHA-256, the error
  type.
- **Phase 5's `pdfkit::text::ImageOccurrence`** and the content interpreter, for
  the listing step only. If phase 5 has not merged when the decoders are green,
  this phase pushes `Build-Phase: 6 incomplete` at the split point of §7, naming
  the listing step, and the team lead resumes it after phase 5 merges. The
  listing never walks the content stream with a second walker, because one
  interpreter serves extraction and redaction (CLAUDE.md §3.2).
- `src/api/` and `cli/` stubs from phase 1, `docs/ABI.md`'s conventions.
- `tests/pdfium/` and `tests/dataset/run.py` from phase 2.
- `build-3.md` §9 and, for the listing step, `build-5.md` §9.

## 2. Components

Namespace `pdfkit::images`, one class per file in `src/images/`.

| File | What it holds | Lines | Reference and status |
|---|---|---|---|
| `third_party/libjpeg-turbo/` | The pinned release's source with its `LICENSE.md` (BSD-3, IJG and zlib terms), built through its own CMake with `WITH_SIMD=OFF` (no NASM on the laptop; recorded in `build/toolchain.md`), `ENABLE_SHARED=OFF`, `WITH_TURBOJPEG=OFF`, statically linked into `pdfkit`; the release and SHA-256 of the upstream archive in `third_party/inventory.tsv` | vendored | https://github.com/libjpeg-turbo/libjpeg-turbo (recalled; the release chosen is the latest stable on the day, verified and recorded) |
| `third_party/openjpeg/` | The pinned release's source with its `LICENSE` (BSD-2), only `openjp2` built, codec tools and executables off, statically linked; inventory row | vendored | https://github.com/uclouvain/openjpeg (recalled; verified and recorded) |
| `image_identity.hpp/.cpp` | `img:` plus the first 16 hex digits of SHA-256 over the encoded bytes and the decisive entries of CLAUDE.md §4 serialised canonically (sorted keys, normalised numbers); an inline image over its data and its abbreviated dictionary with the abbreviations expanded first, so an inline and an XObject image of the same bytes and parameters share an identity | 200 | CLAUDE.md §4; design record §4.5 (read) |
| `image_info.hpp/.cpp` | An image's description: identity, encoding (the first image filter or `none`), width, height, colour space, bits per component, `/Decode`, `/Interpolate`, `/ImageMask`, `/SMask`, `/Mask` (array or stream), `/SMaskInData`, decodable or the warning code of `docs/DESIGN.md` §19 | 200 | ISO 32000-1 §8.9.5 (recalled) |
| `pdf_function.hpp/.cpp` | Functions of type 0 (sampled, with `/Encode`, `/Decode`, `/Size`, 1 to 32 bits), 2 (exponential), 3 (stitching, recursion bounded) and 4 (the PostScript calculator: a bounded operand stack of 100, a bounded operator count per evaluation, the arithmetic, relational, boolean and stack operators, `if` and `ifelse`), with a cache of the last inputs | 650 | ISO 32000-1 §7.10 (recalled); pdf.js `src/core/function.js` (Apache-2.0) as the reference to read beside it (recalled) |
| `colour_space.hpp/.cpp` | `DeviceGray`, `DeviceRGB`, `DeviceCMYK`, `Indexed` (base and lookup string or stream, `hival` clamped), `ICCBased` read by `/N` with `/Alternate` (no colour management), `CalGray`, `CalRGB` (read as their device equivalents), `Lab` (to sRGB through the stated formula), `Separation` and `DeviceN` through the tint transform into the alternate space, `Pattern` refused for images; the naive CMYK to RGB conversion stated in the header (`r = 1 - min(1, c + k)` and its kin, an author decision named in `docs/DESIGN.md` §9) | 600 | ISO 32000-1 §8.6 (recalled) |
| `sample_unpacker.hpp/.cpp` | Rows of 1, 2, 4, 8 and 16 bits per component unpacked with row padding, `/Decode` arrays applied (including the inverted `[1 0]` of a stencil), output 8-bit or 16-bit per component | 250 | ISO 32000-1 §8.9.5.2 (recalled) |
| `masks.hpp/.cpp` | `/SMask` (a separate gray image, its own `/Matte` noted), `/Mask` colour-key ranges, `/Mask` stencil streams, `/ImageMask true` images (painted in the fill colour of the occurrence, emitted as a 1-bit alpha PNG), `/SMaskInData` for JPEG 2000; a mask of a different size than its image is emitted at its own size, never resampled, because resampling invents pixels | 350 | ISO 32000-1 §11.6.5 (recalled) |
| `dct_decoder.hpp/.cpp` | The libjpeg-turbo adapter: a memory source, a custom error manager whose `error_exit` `longjmp`s to a `setjmp` in a C function with no C++ object in its frame, so no destructor is skipped (the rule is stated in the header: no `longjmp` crosses a frame holding a C++ object or a resource; the C frame owns only the decompressor, destroyed on both paths); grayscale, RGB, YCbCr, CMYK and YCCK; the Adobe APP14 inversion of CMYK; `/Decode [1 0 1 0 1 0 1 0]` honoured; bounded by the budget before `jpeg_start_decompress` | 400 | libjpeg-turbo's `libjpeg.txt` on error handling (recalled); the Adobe inversion is pdf.js and PDFium behaviour (recalled) |
| `jpx_decoder.hpp/.cpp` | The OpenJPEG adapter: a memory stream, `J2K` and `JP2` detected by signature, reduction 0, components mapped to the PDF colour space (the codestream's own when `/ColorSpace` is absent), `/SMaskInData` returning the last component as the mask, sub-sampled components upsampled by replication with the factor stated, the decoded area bounded by the budget before the tile data is decoded, OpenJPEG's message callbacks silenced into the warning | 400 | OpenJPEG API documentation (recalled) |
| `ccitt_decoder.hpp/.cpp` | Written: a port of pdf.js `src/core/ccitt.js` (Apache-2.0, 1,059 lines measured by the design record): Group 3 one-dimensional and two-dimensional (`/K` > 0), Group 4 (`/K` < 0), `/EncodedByteAlign`, `/BlackIs1`, `/EndOfLine`, `/EndOfBlock`, `/Columns`, `/Rows`, `/DamagedRowsBeforeError`; the mode and run-length tables as `constexpr` arrays; a damaged row read as white and counted. Its header names the source file, the commit read and the licence | 1,300 | https://github.com/mozilla/pdf.js/blob/master/src/core/ccitt.js (read by the design record: 1,059 lines; the file itself to be read when cloned) |
| `arithmetic_decoder.hpp/.cpp` | The MQ arithmetic decoder JBIG2 uses (the QE table, `INITDEC`, `DECODE`), ported from pdf.js `src/core/arithmetic_decoder.js` (Apache-2.0) | 200 | pdf.js (recalled path; verified when cloned) |
| `jbig2_decoder.hpp/.cpp`, `jbig2_segments.hpp/.cpp`, `jbig2_regions.hpp/.cpp` | Written: a port of pdf.js `src/core/jbig2.js` (Apache-2.0, 2,584 lines measured by the design record): segment headers, `/JBIG2Globals`, page information, end of stripe, generic regions (templates 0 to 3, TPGDON, the arithmetic and MMR paths, MMR through `ccitt_decoder`), generic refinement regions, symbol dictionaries (with and without Huffman, refinement aggregation), text regions (Huffman and arithmetic, the standard and custom Huffman tables), pattern dictionaries and halftone regions, the region combination operators; every bitmap dimension checked against the budget before allocation; an unknown or unsupported segment is a warning on the image, not a crash. Split across three files by concern (segment parsing, region decoding, the decoder's entry) | 3,500 | https://github.com/mozilla/pdf.js/blob/master/src/core/jbig2.js (read by the design record: 2,584 lines plus the arithmetic decoder; the file itself to be read when cloned) |
| `pixel_image.hpp/.cpp` | The decoded image every treatment of phase 9 reads: width, height, components, bits (8 or 16), samples in the output colour space (gray, RGB or the source CMYK kept beside its RGB conversion for re-encoding), an optional alpha | 200 | this file |
| `image_decoder.hpp/.cpp` | The dispatch: the encoding to the decoder, then the colour space, the decode array and the masks; the result a `PixelImage` or the warning code (`undecodable_image` with the reason, `image_in_pattern` for an image reached only through a pattern or soft mask) | 300 | `docs/DESIGN.md` §9, §19 |
| `png_writer.hpp/.cpp` | Gray, gray with alpha, RGB, RGB with alpha; 1, 8 and 16 bits; zlib deflate at a fixed level and strategy; filter type chosen by a fixed rule (`None` for 1-bit, `Paeth` otherwise); no `tIME`, `tEXt` or gamma chunks, so the bytes depend on the pixels alone | 250 | the PNG specification (recalled) |
| `image_extractor.hpp/.cpp` | Lossless source to PNG; DCT source to its original encoded bytes unchanged (the JFIF or Adobe stream exactly as stored, after the non-image filters before it); a mask to its own PNG | 200 | `docs/DESIGN.md` §9 |
| `image_listing.hpp/.cpp` | **The listing step (needs phase 5):** per image identity, the info and every occurrence from the interpreter (page, ordinal, device box, form path), images shared across pages listed once with several occurrences, an image in a pattern or a soft mask listed with its warning code | 250 | design record §4.5 (read) |
| `src/api/images.cpp`, `src/api/image_data.cpp`, `cli/verbs/images.cpp` | The two catalogue functions and the verb, JSON shapes written into `docs/ABI.md` | 350 | CLAUDE.md §2, §3.6 |

## 3. Tests

Unit tier, `tests/unit/images/`, fixtures written in code.

| File | Holds |
|---|---|
| `colour_space_test.cpp` | Each colour space over known samples, `Indexed` with a short lookup, `Separation` through each function type, `DeviceN` with four components |
| `pdf_function_test.cpp` | Types 0, 2, 3 and 4 over known values; a type 4 program past the operator bound refused; a type 3 recursion past the bound refused |
| `sample_unpacker_test.cpp` | Each bit depth with row padding, `/Decode` inversion |
| `masks_test.cpp` | `/SMask` of the same and of another size, colour-key, stencil, `/ImageMask` |
| `dct_decoder_test.cpp` | Gray, RGB, CMYK and Adobe-inverted CMYK JPEGs **encoded in the test by libjpeg-turbo's own compressor** from known pixels and decoded within a stated tolerance; a truncated JPEG returning a warning with no leak under the sanitizer |
| `jpx_decoder_test.cpp` | A small J2K and a JP2 codestream encoded in the test by OpenJPEG's encoder if built for tests only, otherwise byte arrays from OpenJPEG's own permissively licensed conformance data with their source named |
| `ccitt_decoder_test.cpp` | Round trip through a minimal Group 4 and Group 3 encoder written in the test (encoders are test code only, because a test encoder is the cheapest proof of a decoder); `/BlackIs1`; `/EncodedByteAlign`; a damaged row |
| `jbig2_decoder_test.cpp` | Byte-array vectors from permissively licensed sources, each with its source named in the test (pdf.js's test vectors under Apache-2.0 if present; else small streams hand-assembled in the test from the specification's segment layout for a generic region with each template, an MMR generic region, and a symbol dictionary with a text region); a segment with an absurd bitmap size refused by the budget. A JBIG2 encoder is out of scope, so the dataset is the main proof (§4) |
| `png_writer_test.cpp` | The bytes of a 2×2 image of each type against expected bytes written in the test; decoded back through zlib, the pixels equal |
| `image_identity_test.cpp` | The same bytes stored twice share an identity; a changed `/Decode` changes it; an inline image and an XObject of the same bytes share it |
| `image_listing_test.cpp` | One image drawn on three pages and in a form: one identity, four occurrences with the right ordinals (needs phase 5) |

**Dataset.** The phase fills the `images` column for every row: `decoded`
(every occurrence decoded), `partial:<encodings that failed>`, or `n/a`. It
adds `dataset/tools/synth/images.py` for any image tag under its minimum in
`dataset/taxonomy.tsv` (per encoding, per colour space, soft mask, stencil,
inline, shared across pages, in a form): JPEG, JPEG 2000 and CCITT samples can be
synthesised; JBIG2 relies on public sets (PDFium's corpus holds 111 JBIG2 files,
per the design record, read).

**PDFium.** `tests/pdfium/image_agreement.py` loads each image object through
PDFium (`FPDFImageObj_GetBitmap` for the decoded bitmap; recalled API name,
verified against PDFium's public headers `fpdf_edit.h` when the binding is
written), aligns it with pdfkit's decoded image by page and draw ordinal, and
compares pixels.

## 4. Gate

Over every dataset file whose `open` expectation is `opens` or `repaired`:

1. **Decoded**: at least **98%** of image occurrences decode, reported per
   encoding (none, Flate, LZW, RunLength, DCT, JPX, JBIG2, CCITT) and per colour
   space; every undecoded one carries its warning code (no silent omission:
   occurrences listed equal occurrences the interpreter recorded, 100%).
2. **Bitonal agreement**: JBIG2 and CCITT images agree with PDFium's decoded
   bitmap **pixel for pixel on 100%** of the images PDFium decodes.
3. **Continuous-tone agreement**: DCT, JPX and Flate images agree with PDFium's
   within a mean absolute difference of **at most 2 per channel on at least
   99%** of the images both decode.
4. **Stability**: identities and PNG bytes identical across two runs on 100%.
5. **Safety**: no crash, hang (30 s per file) or sanitizer report over the
   dataset; the unit tier and `python build/check.py` green.

Commands:

```
cmake --preset mingw-release && cmake --build --preset mingw-release
python tests/dataset/run.py --functions open,images --out dataset/reports/phase6-<date>/
python tests/pdfium/compare.py --run phase6-<date> --images
ctest --preset unit && ctest --preset sanitize
python build/check.py
```

The report (`images.tsv`: set, id, identity, encoding, colour space, size,
decoded, difference against PDFium; `summary.md` per encoding) goes into the
merge commit message.

## 5. Kill criterion and the question to the owner

**Quantity:** the share of JBIG2 images agreeing with PDFium pixel for pixel.
**Threshold:** below 95%.

**Question, word for word with the numbers filled in:** "pdfkit's JBIG2 decoder
agrees with PDFium pixel for pixel on N of M JBIG2 images; the others fail on
<the named segment types>. Do you want (a) one more JBIG2 session on those
segment types (about 6 to 10 hours, about $100 to $180), (b) to ship JBIG2 as
undecodable with a warning, so those images can be removed but not boxed or
blurred (no extra cost now), or (c) research a permissively licensed JBIG2
decoder to vendor instead (about 2 hours of research, about $30, then a vendoring
session if one exists; jbig2dec is AGPL and stays disqualified)?"

**Unattended action:** write the question with the evidence (the summary, the
failing images by segment type) into `OPEN-QUESTIONS.md`; merge the branch if
checks and the unit tier are green, with trailer `Build-Phase: 6 gate-failed`;
phases that depend on it (9) proceed and report their gates with that caveat.

## 6. Rollback

Reverting the merge removes the images layer, the two vendored codecs with their
inventory rows and notices, and returns `pdfkit_images` and `pdfkit_image_data`
to their phase-1 stubs. Nothing persisted changes shape.

## 7. Execution order inside the phase

1. Run `python build/check.py` and `ctest --preset unit` on the base.
2. Vendor libjpeg-turbo and OpenJPEG: download the pinned releases into the
   scratchpad, verify, copy the source with the licence files, add the inventory
   rows, the notices and the CMake wiring; build; run the checks
   (`build/check_third_party.py` recomputes the hashes).
3. `image_identity`, `image_info`, `pdf_function`, `colour_space`,
   `sample_unpacker`, `masks`, `pixel_image`, `png_writer`, `dct_decoder`,
   `jpx_decoder`, `image_decoder`, `image_extractor`, with failing tests first.
4. **Split point 1.** Push with `Build-Phase: 6 incomplete` if out of room,
   naming CCITT, JBIG2 and the listing as remaining.
5. `ccitt_decoder` and its tests.
6. **Split point 2.** As above, naming JBIG2 and the listing.
7. `arithmetic_decoder`, `jbig2_*` and their tests.
8. **Split point 3.** If phase 5 has not merged, push here with
   `Build-Phase: 6 incomplete` naming the listing step; the team lead resumes
   this branch, rebased on `main`, once phase 5 merges.
9. `image_listing`, `src/api/images.cpp`, `src/api/image_data.cpp`,
   `cli/verbs/images.cpp`, the `docs/ABI.md` sections, `dataset/tools/synth/images.py`.
10. The gate run of §4; read every failing image and fix decoder faults.
11. Documentation lines this phase invalidates: `docs/ABI.md`, `docs/SOURCES.md`
    (pdf.js commit and files read, libjpeg-turbo and OpenJPEG releases),
    `third_party/inventory.tsv`, `THIRD-PARTY-NOTICES.md`, `build/toolchain.md`
    (the SIMD-off note, added as a row), and `docs/DESIGN.md` §9 only where the
    code proves a rule wrong, recorded in §9.

## 8. Opening prompt

> Read `CLAUDE.md`, `docs/DESIGN.md` §3, §6 (image occurrences), §9 and §19,
> `docs/ABI.md`, `docs/SOURCES.md`, all of `build-6.md`, and the deviations
> sections (§9) of `build-1.md` to `build-3.md` (and of `build-5.md` before the
> listing step). Before planning, read what landed in `src/syntax/` (streams,
> the filter chain's hand-back of image-encoded bytes, inline images in the
> content lexer), `src/platform/` (budget, SHA-256, checked arithmetic), the
> `src/api/` and `cli/` stubs, `tests/dataset/run.py` and `tests/pdfium/`.
> Work in a worktree of your own on branch `phase/6-images` created from `main`;
> phases 4, 5 and 7 may run at the same time in other worktrees, so touch only
> the paths this file's header owns and add, never rewrite, rows in the shared
> files it names. Run `python build/check.py` and `ctest --preset unit` before
> you start and before you finish; if the base is red, repair it in a commit of
> its own and record the repair in §9. Write failing tests first. Download the
> two codec releases only from their official repositories, pin them by SHA-256
> in `third_party/inventory.tsv`, and port JBIG2 and CCITT from pdf.js
> (Apache-2.0), naming the file and commit in each header. Every external fact
> carries its source and whether you read it or recalled it, in the comment and
> in `docs/SOURCES.md`. The dataset is in the main checkout's `dataset/files/`
> and PDFium in its `build/cache/pdfium/`, reached through
> `git rev-parse --git-common-dir`. No step waits for the owner: when the kill
> criterion of §5 trips, write its question with the evidence into
> `OPEN-QUESTIONS.md` and finish; when phase 5 has not merged at the listing
> step, push `Build-Phase: 6 incomplete` naming it and stop. Finish by pushing
> the branch with a last commit whose message carries `Build-Phase: 6 done`, or
> `Build-Phase: 6 incomplete` (a split point of §7) or
> `Build-Phase: 6 gate-failed`, with the gate's numbers in the message. Never
> merge into `main`; the team lead merges. Record every deviation from this
> file in its §9 and in every downstream build file it affects (`build-9.md`,
> `build-10.md`) in the same change.

## 9. Deviations recorded by this phase

1. **Two sessions, split at point 3.** Phase 5 had not merged when the
   decoders were green, so the decoders merged first (`Build-Phase: 6
   incomplete`) and the listing step was done after phase 5 merged, on a
   branch rebased onto it: `image_listing`, `image_info`, `pdfkit_images`,
   the images verb's `--out`, the dataset verb's `images` section, the
   `pdfkit_images` section of `docs/ABI.md` (§4.10) and the listing half of
   the gate (item 17).
2. **`images::ImageCatalogue`** (`src/images/image_catalogue.{hpp,cpp}`,
   beyond §2) lists every image XObject the pages reach through their
   resources, their forms' resources and the resources of tiling patterns and
   soft-mask groups (those marked `in_pattern`), by identity, without reading
   any content stream. `pdfkit_image_data` resolves an identity against it;
   the document handle keeps it (`pdfkit_document::images`, one member and one
   include in `src/api/handles.hpp`). The listing step keeps it for
   `pdfkit_image_data` (an occurrence-only image list would miss images a
   caller names from an earlier listing) or replaces it with the
   interpreter's occurrences. A form, pattern or group stream is entered once
   per page and context, because a tree of forms sharing forms
   (`pdfjs-0045`) made the plain recursive walk run for minutes.
3. **Gate measured before the listing step**, by a census tool
   (`tests/dataset/image_census.cpp`, target `pdfkit_image_census`, beside
   phase 4's font census) driven by `tests/pdfium/image_agreement.py`. An
   "occurrence" in its gate 1 is an image times a page whose resources reach
   it, not an interpreter occurrence. PDFium's bitmap
   (`FPDFImageObj_GetBitmap`) is aligned with pdfkit's image by the SHA-256 of
   the encoded data (`FPDFImageObj_GetImageDataRaw`). Not compared, each
   listed in the report: images whose soft mask has `/Matte` (pdfkit
   un-premultiplies as §9.4 says, PDFium's bitmap keeps the premultiplied
   colours: 16 images) and 1-bit Indexed images, which PDFium hands back as
   palette indices without the palette. Stencils are compared as the mask
   they paint (255 where they paint). The census gets four times the 30 s per
   file (it decodes each image three times and writes its pixels) and a
   64 GiB document budget; the per-stream limit is unchanged.
4. **Gate numbers** (888 files opened or repaired, run of 2026-10-25, branch
   tip before the rebase): decoded 4,396 of 4,402 images (99.86%), 6,086 of
   6,092 page occurrences (99.90%), every failure with its fault; bitonal
   agreement 491 of 492 (99.80%; JBIG2 16 of 16, CCITT 475 of 476);
   continuous-tone agreement 3,663 of 3,697 (99.08%); identities and PNG
   bytes equal over two runs on every file; no crash or hang; `python
   build/check.py` (15 groups), the unit tier and both sanitizer tiers green
   at the branch tip. The one bitonal
   disagreement is `qpdf-0026` (a 2500 x 3250 Group 4 page): its first coding
   line reads, by T.4's tables, as horizontal mode with a white run of 0 and a
   black run of 2496 + 4, so pdfkit's page is black where PDFium's bitmap and
   rendering are white; left as a PDFium difference, not fixed. The 34
   continuous-tone disagreements are colour management (27 `ICCBased` images,
   `OPEN-QUESTIONS.md` §2.8), PDFium's Lab reading (3, item 8), 2 images of
   `pdfjs-0048` (Indexed over an ICC base, MAD 3.6 and 4.4), and 2 of
   `pdfpig-0009` and `pikepdf-0038` (Indexed, not investigated). Undecoded: 6
   (a PNG stored as DCT twice, a filter `/XXXDecode`, an empty stream, an
   LZW image without `/ColorSpace`, a DeviceN without colorant names).
5. **CMYK converts through PDFium's Adobe CMYK table**
   (`src/images/adobe_cmyk.{hpp,cpp}`, ported from PDFium's
   `cfx_cmyk_to_srgb.cpp`, BSD-3, notice in `THIRD-PARTY-NOTICES.md` §8)
   instead of the naive complement of §2 and `docs/DESIGN.md` §9.4, which
   failed the continuous-tone gate on every real CMYK image; DESIGN §9.4 says
   so and `OPEN-QUESTIONS.md` §2.7 asks the owner to confirm.
6. **An Adobe APP14 marker inverts nothing**: the dataset's YCCK JPEGs agree
   with PDFium uninverted (MAD 0.65) and not inverted (MAD 248), so §2's
   "the Adobe inversion of CMYK" and DESIGN §9.3's row are corrected. A JP2
   palette is left unapplied for an Indexed space
   (`OPJ_DPARAMETERS_IGNORE_PCLR_CMAP_CDEF_FLAG`), as PDFium leaves it.
7. **The DCT guard is `src/images/dct_guard.c`**, the library's one C source,
   so the C++ checks (format, tidy, headers, layout) do not read it; it keeps
   the licence and purpose headers by hand, and `src/images/CMakeLists.txt`
   names it with its reason.
8. **Lab follows ISO 32000-1**: `/Range` maps the a* and b* samples, where
   PDFium reads every 8-bit sample as the byte minus 128, so the dataset's
   three Lab images differ from PDFium by about 12.
9. **Identity**: the hash covers the encoded bytes as stored after
   decryption, with no filter applied (stable under renumbering, encryption
   and page selection and cheap to list); a stream inside a decisive entry
   (an ICC profile, a lookup, a function, `/JBIG2Globals`) counts by the
   SHA-256 of its stored bytes and its entries, a mask stream by its own
   identity.
10. **`pdfkit_image_data`** takes `{"mask": bool, "decode_jpeg": bool}`
    (`decode_jpeg`, beyond §0, gives a PNG of a JPEG's pixels, which the gate
    and the treatments need); an image that does not decode is refused with
    the new refusal code `undecodable_image` (`platform::ErrorCode`,
    `src/api/guarded.cpp`, DESIGN §19, ABI §2 and §4.8); an unknown identity
    or a mask of an image without one is `invalid_argument`.
11. **Files beyond §2**: `decode_fault.hpp` (the codecs' internal fault),
    `adobe_cmyk.*`, `image_catalogue.*`, `dct_guard.c`; from the bitonal
    helper session, `jbig2_bitmap.*` and `jbig2_huffman.*` (keeping
    `jbig2_regions.cpp` under 1,500 lines) and `CcittDecoder::Result`'s
    `bytes_consumed` (the halftone MMR planes need it); tests
    `image_fixture.*` (fixtures and the libjpeg-turbo encoder),
    `image_catalogue_test.cpp`, `image_data_test.cpp` (the C ABI, kept beside
    the layer's tests), `arithmetic_decoder_test.cpp`, `mq_test_encoder.*`,
    `ccitt_test_encoder.*`, `jbig2_conformance_vectors.*` (28 streams of
    pdf.js's `bitmap-*.pdf`, each pinned by a hash).
12. **The JBIG2 and CCITT ports** read pdf.js at commit
    `fe48736846e2043696738bd54435ebb5ab24ecff`, the last before pdf.js
    replaced its JavaScript decoders with a build of PDFium's; they decode
    more than pdf.js does (intermediate and refinement regions, the five
    combination operators, Huffman with refinement, the halftone skip bitmap,
    both file organisations) and agree with PDFium on 107 of 108 images of
    pdf.js's JBIG2 test files (the exception is a stream with a file header
    PDFium shows blank).
13. **Vendoring**: libjpeg-turbo 3.2.0 and OpenJPEG 2.5.4 are built by our
    own `CMakeLists.txt` in each folder from the release's source lists, not
    by the releases' CMake; the `add_subdirectory` lines went into the root
    `CMakeLists.txt` (there is no `third_party/CMakeLists.txt`), and both
    targets are public dependencies of `pdfkit_core`, so the unit tier
    encodes its fixtures with the libraries' own encoders. OpenJPEG is built
    without threads.
14. **No `dataset/tools/synth/images.py`**: every image tag of the taxonomy
    is at or above its minimum (`python dataset/tools/report.py`).
15. **The manifest's `expect_images` column** is written by
    `tests/pdfium/image_agreement.py --write-manifest` from the census, over
    resource-reached images: six files become `partial:<encoding>` (the six
    undecoded images of item 4) and `qpdf-0029`, refused at open, `n/a`; a
    file whose resources reach no image keeps phase 2's value, because inline
    images are not reached that way. The listing step kept the column and
    changed one row: `opf-0024` becomes `partial:none`, because its content
    draws five inline images whose dictionaries are broken (no `/Width`, no
    `/ColorSpace`), which the resource walk could not see.
16. **The listing** (`src/images/image_listing.{hpp,cpp}`) reads each asked
    page once through `text::TextReader::interpret` and keeps it on the
    document handle (`pdfkit_document::listing`): occurrences grouped by
    identity (an inline image hashed from its dictionary and data per draw),
    sorted by page and ordinal once per call; the images a page reaches only
    inside the patterns and soft-mask groups it sets
    (`PageContent::unwalked`) are found by walking those resources
    (`ImageCatalogue`'s second constructor) and listed with `pattern_pages`
    and `image_in_pattern`, never numbered; an inline image inside a
    pattern's content is not reached. A page whose resources name no
    XObject, pattern or graphics state and whose content holds no `BI` is
    not interpreted (a text-only document lists in under a second). Each
    listed image is decoded once to fill `decodable`; a budget limit met
    there is the image's fault, a limit met reading content stays the
    document's refusal. `images::ImageInfo` (§2's `image_info`) describes
    the dictionary without decoding. `pdfkit_image_data` finds an inline
    image by reading every page when no XObject has the identity
    (`ImageExtractor::extract_inline`). The JSON gains `source`,
    `filters`, `components`, `interpolate`, `pattern_pages` and a per-image
    `warnings` list beyond §0. The images verb prints that JSON and, with
    `--out`, writes `<digits>.jpg|.png` and `<digits>.mask.png` and adds
    `files`; the dataset verb's `images` section is
    `cli/verbs/dataset_images.cpp` (one line in `cli/verbs/dataset.cpp`).
17. **The listing gate**, `python tests/pdfium/image_agreement.py
    --listing` (the C ABI through `pdfkit images`, 888 files): 36,456 of
    36,467 occurrences decode (99.97%; per encoding in its report), every
    undecodable image carries `undecodable_image` (0 without it), 83 images
    are reached inside patterns or soft masks; each page's occurrence count
    equals the image objects of PDFium's page (forms descended) on 1,991 of
    1,999 pages (99.60%). The 8 pages that differ: `opf-0019` and
    `mutate-0046` page 13 (400 against 402), `opf-0024` pages 2, 8 and 14
    (the broken inline images, one more or one fewer each),
    `pikepdf-0005` page 3 and `safedocs-0005` page 1 (one draw PDFium does
    not count), and `pdfpig-0015` page 1, where PDFium reports one image 30
    times and the content draws it once (not investigated further; the
    interpreter is phase 5's). The dataset tier (`--checks open,images`)
    passes every `expect_images` row and has no hang.

### Deviations of earlier phases that affect this one

From `build-5.md` §9 (the listing step reads these):

- The occurrences come from `pdfkit::text::TextReader` (`src/text/text_reader.hpp`): construct one per document (`TextReader reader(document)`) and call `reader.interpret(page_number)`, which returns a `text::PageContent` (`src/text/page_content.hpp`) without the layout. `PageContent::images` holds one `text::ImageOccurrence` (`src/text/image_occurrence.hpp`) per image draw on the page, already in draw order through forms: `images[i].ordinal == i`, numbered from 0 per page. An image inside a pattern or a soft mask is not walked and so is not among them: `PageContent::unwalked` lists, per page and in the order set, every tiling pattern set as a colour (`scn`/`SCN` with a pattern name) and every soft-mask group set by an ExtGState (`gs` with `/SMask << /G ... >>`), each a `PageContent::Unwalked` with `kind` (`pattern` or `soft_mask`), `content` (the resolved pattern stream or group form), `reference`, `resources` (its own `/Resources`, null when it has none), `stream`, `operation` and `form_path`; list the images its resources draw with `image_in_pattern`, never numbered.
- Each occurrence has `page` (1-based), `ordinal`, `kind` (`xobject` or `inline_image`), `ctm` (the image is the unit square under it), `box` (that square's device bound), `clip` (the clip bound in force; `Rect::is_infinite()` when nothing clips), `form_path` (the form XObjects it was reached through by object number, outermost first; empty in the page's own content), `optional_content_off`, and `stream` and `operation` (the walk in `PageContent::streams` and the index of the `Do` or `BI` among its operations, which a removal deletes through `text::ContentSerialiser::write` with an edit). An XObject occurrence carries `reference` (number 0 for a direct stream), `resource_name` and `image`, the resolved stream `syntax::Object`, whose `stream()` gives `Document::decode`'s input; an inline occurrence carries `dictionary` (as written, abbreviated keys kept), `colour_spaces` (the resolved `/ColorSpace` resource dictionary of the content that drew it, null when none, for a named inline colour space), `data` (its bytes, copied) and `data_start`/`data_end` in the walk's bytes.
- One image drawn twice gives two occurrences; one form drawn twice is walked twice (two `PageContent::streams` entries with the same `form`), so its images are numbered twice, as `docs/DESIGN.md` §9.2 wants. Identity is the image layer's: group occurrences by identity, not by `reference`.
- The text layer charges `Document::decode` once per form per `TextReader` (it caches decoded forms), and decodes no image data.

From `build-1.md` §9:

- `cli/verbs/images.cpp` takes `--out <folder>` without checking it first; the first image write reports a folder that cannot be written, and `std::filesystem` stays out of the CLI because it alone tripled `pdfkit.exe` (item 7).
- `pdfkit_image_data(doc, identity, options_json, out)` writes PNG or JPEG bytes, not JSON, into `out` on success (item 4).
- `third_party/inventory.tsv` has the columns `path, name, version, upstream, archive, archive_sha256, tree_sha256, licence, test_only`; compute a tree hash with `python build/check_third_party.py --print-tree-hash third_party/<name>` (our `CMakeLists.txt` in the folder is excluded) and cache the upstream archive in the main checkout's `build/cache/third_party/` (`build-1.md` §9 item 11).
- The sanitizer tier is two runs: `ctest --preset sanitize` (GCC, UBSan in trap mode) and `ctest --preset clang-sanitize` (llvm-mingw, ASan and UBSan) (`build-1.md` §9 item 8).

From `build-3.md` §9:

- The object model is `syntax::Object` (nested `Reference`, `Number`, `String` with its literal-or-hex flag, `Array` = `std::vector<Object>`), `syntax::Dictionary` (insertion-ordered) and `syntax::Stream` (dictionary, raw byte range, owner); all immutable and shared, built through the `make_*` factories (item 3).
- `syntax::Document::open(bytes, Limits)` returns the document; `object(reference)`, `resolve(object)` and `resolve_entry(dictionary, key)` resolve (strings come back decrypted), `decode(stream, page)` decrypts and runs the filters up to the first image codec (`Filters::Result::remaining` names the codecs left), `content_pieces(contents, page)` decodes a `/Contents` value, `pages()` holds `syntax::Page` (boxes, rotation, resolved resources, contents), `repair(reason)` and `warn(code, message, page)` record (items 3, 4).
- `syntax::Resources(document, page.resources, page.number)` looks a resource up by category and name, with the form fallback in `for_form`; `syntax::ContentLexer(pieces, document.limits(), "page N")` yields `Operation`s (operator, operands, byte range, `operator_start`; an inline image is one `BI` operation with its data range) and `locate(offset)` maps back to the piece (item 10).
- `Document::decode` charges the budget on every call, so a layer that decodes a shared stream more than once caches the decoded bytes (item 11).
- An image XObject's encoded bytes are `Document::decode(stream).bytes`, with `remaining` and `remaining_parameters` naming the codec; an inline image's data is the `BI` operation's `data_start` to `data_end` range of `ContentLexer::bytes()` (items 3, 10).
