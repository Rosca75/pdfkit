# build-9 — Image treatments

| Field | Value |
|---|---|
| Phase | 9 |
| Branch | `phase/9-redact-images` |
| Depends on | 6, 7, 8 |
| Parallel with | nothing (it merges after 8, whose edit-plan and write-pipeline changes it extends) |
| Model | `opus` |
| Owns | `src/images/treatments/`, `src/redact/image_*`, `src/redact/edit_plan.hpp`, `src/redact/edit_plan.cpp` (additive), `src/images/bitmap_font_data.inc`, `build/fonts/bitmap_font.py`, `build/fonts/check_regenerated.py` (extended), `src/api/redact_images.cpp`, `cli/verbs/redact.cpp` (the images section), `tests/unit/treatments/`, `dataset/tools/synth/images/`, `tests/dataset/gates/phase9.py`, `tests/pdfium/images.py` |
| Size | about 3,000 lines of C++ including tests, plus about 300 lines of Python (the bitmap font rasteriser, the image synthesiser, the gate) |
| Price | estimate: 8 to 11 hours of session time and roughly $130 to $190 of model spend at list prices. Calibrated from the design record's stage 3 (treatments and re-encode 500 lines, image decode and colour spaces 900, tests about 1,800 lines of C# for the stage; 20 to 25 h and $300 to $450 for the whole stage) and change-12's per-batch figures, scaled by 1.3 for C++ |

## 0. What this phase ships

Image redaction per image identity: a box (with an optional caption drawn into
the pixels), a blur that destroys, or removal of the image and of every
operator that draws it; and a box over a region of an image for a match the
caller found in its pixels.

Catalogue row of `CLAUDE.md` §2 delivered:

- **6 Redact images**: `pdfkit_redact_images` records treatments per image
  identity on an edit handle (`box` with a rectangle and an optional caption,
  `blur`, `remove`, and `box` with a region rectangle in image-pixel or page
  space) and answers per treatment the rectangles that will be painted and the
  occurrences reached. The writer applies them at the image-treatment stage of
  the pipeline.

CLI: the `redact` job file's `images` section is accepted (it was refused with
an actionable message until this phase).

## 1. What it reads from earlier phases

Read as landed, and read `build-6.md` to `build-8.md` §9 for deviations first.

| From | What | Why |
|---|---|---|
| phase 6 | `src/images/` image identity, the decoders (Flate with predictors, DCT, JPEG 2000, JBIG2, CCITT), the colour-space reader with `/Decode`, the mask reader (`/SMask`, colour-key `/Mask`, stencil `/Mask`, `/ImageMask`), the PNG writer, the `undecodable_image` warning, `pdfkit_images` occurrences with device boxes | a box or a blur needs the decoded samples; the identity is the key of a treatment |
| phase 5 | `src/text/` image occurrences (page, ordinal, form path, the CTM at the `Do` or `BI`), `ContentSerialiser` | removal deletes the drawing operators; a page-space region maps into pixel space through the occurrence's CTM |
| phase 7 | `src/redact/edit_plan.*`, `src/redact/write_pipeline.*` (the image-treatment stage), `StreamCopier`, `Reachability`, `WriteReport`, `tests/pdfium/render.py` | a treated image is a rewritten stream; a removed XObject leaves through the reachable sweep |
| phase 8 | the edit plan as extended for text, the self-check, `third_party/liberation-fonts/`, `build/fonts/` | the caption's bitmap font is cut from the same font; the self-check keeps running on every output |
| vendored | libjpeg-turbo's encoder (phase 6 vendored it for decoding), zlib deflate | re-encoding |

## 2. Components

| File | What it holds | Lines | Reference |
|---|---|---|---|
| `build/fonts/bitmap_font.py` | rasterises the placeholder alphabet from Liberation Sans Regular at a set of pixel heights (8, 12, 16, 24, 32, 48, 64) with fontTools' pen protocol and a scanline fill of the quadratic outlines (no hinting, no anti-aliasing: a caption is drawn in one colour), and emits `src/images/bitmap_font_data.inc` (per height and character a bitmap and an advance) with its SHA-256; held by the regeneration check | 200 | fontTools pens (recalled); Liberation Sans, SIL OFL 1.1, which permits derived bitmaps (OFL FAQ, design record Appendix A, stream 3, read) |
| `src/images/treatments/pixel_buffer.{hpp,cpp}` | `PixelBuffer`: decoded samples in the source's colour space and bit depth with their component count, width, height and stride; the budget check on width times height times components | 150 | `CLAUDE.md` §9 (bounded allocation) |
| `src/images/treatments/box_painter.{hpp,cpp}` | `BoxPainter`: `docs/DESIGN.md` §14: a filled rectangle in pixel space, the fill colour expressed in the source's colour space (black by default, through an inverse of `/Decode` and of an `Indexed` lookup, choosing the palette entry nearest the colour), clipped to the image | 200 | `docs/DESIGN.md` §14 |
| `src/images/treatments/caption_drawer.{hpp,cpp}` | `CaptionDrawer`: the caption centred in the box with the largest bitmap height that fits, in white or black for contrast with the box colour; a caption outside the alphabet refused with the three-part message | 150 | `docs/DESIGN.md` §14 |
| `src/images/treatments/mosaic_blur.{hpp,cpp}` | `MosaicBlur`: `docs/DESIGN.md` §14: block size at least 1/16 of the smaller dimension and at least 8 pixels; each block filled with its mean; refused for a bitonal image below the size at which a block holds information, with `box` offered in the message | 150 | `docs/DESIGN.md` §14 |
| `src/images/treatments/region_mapper.{hpp,cpp}` | `RegionMapper`: a region in page space mapped through the inverse of an occurrence's image matrix (the CTM at the draw, the unit square to the image's pixel grid, with the vertical flip) into pixel space, rounded outward; a region in pixel space passed through | 120 | ISO 32000-1 §8.9.4 (recalled; verify when writing, label in the comment) |
| `src/images/treatments/reencoder.{hpp,cpp}` | `Reencoder`: Flate in the source colour space and bit depth for a lossless source; JPEG through libjpeg-turbo at quality 90 with the source's component count (CMYK written with the Adobe marker, as the source was) for a DCT source; a bitonal JBIG2 or CCITT source as 1-bit Flate; a JPEG 2000 source as Flate at its decoded depth; the image dictionary rewritten (`/Filter`, `/DecodeParms` and `/Length` replaced, `/Decode` kept where the samples kept their meaning, `/JBIG2Globals` dropped) | 300 | libjpeg-turbo API (recalled; verify against the vendored headers) |
| `src/images/treatments/soft_mask_treatment.{hpp,cpp}` | `SoftMaskTreatment`: the same rectangle applied to the `/SMask` (filled opaque under a box, mosaicked under a blur) and to a colour-key `/Mask` range, because a box over the colour and not over the mask shows the box's edge through the mask's shape | 150 | `docs/DESIGN.md` §14 |
| `src/redact/image_treatment_plan.{hpp,cpp}` | `ImageTreatmentPlan`: per identity one treatment (a second treatment of one identity is refused naming the first); region boxes accumulate per identity; validation of each identity against `pdfkit_images` | 150 | `CLAUDE.md` §4 (treatment) |
| `src/redact/image_remover.{hpp,cpp}` | `ImageRemover`: `docs/DESIGN.md` §14 removal: every `Do` naming the XObject removed from every content stream and form that draws it (the operator only, its `q`/`Q` and `cm` kept, so the graphics state stays balanced), an inline image's `BI ... EI` removed, the XObject's resource entry removed so the reachable sweep drops the object; the occurrence's rectangle left empty | 200 | `docs/DESIGN.md` §14 |
| `src/redact/image_treatment_applier.{hpp,cpp}` | `ImageTreatmentApplier`: the pipeline stage: decode, paint, re-encode, replace the stream in place under the same object number, so every occurrence reaches the treated pixels; an inline image replaced inside its content stream with the same abbreviated keys; the write report's rectangles (the device boxes of every treated occurrence, clipped to the treated region) | 250 | `CLAUDE.md` §3.2 |
| `src/redact/edit_plan.{hpp,cpp}` (extended) | the image treatments; additive only | 50 | — |
| `src/api/redact_images.cpp` | the entry through `api::guarded`, the JSON of `docs/ABI.md`, the refusals of `docs/DESIGN.md` §19 (an unknown identity, box or blur on an undecodable image with `remove` offered, a caption outside the alphabet) | 150 | `docs/ABI.md` |
| `cli/verbs/redact.cpp` (the images section) | the job file's `images` array read and passed through; its refusal sentence removed | 60 | — |
| `tests/unit/treatments/*.cpp` | §3 | 800 | — |
| `dataset/tools/synth/images/` | synthesised files covering what public files rarely hold together: one image drawn on three pages and inside a form, an inline image, an image with a soft mask, one with a colour-key mask, a stencil mask, an `Indexed` image, a 16-bit image, a CMYK JPEG, a JPEG 2000 with `SMaskInData`, a JBIG2 with globals and a CCITT G4 image (both produced by the encoders phase 6 used for its own fixtures, or taken from a permissive public file when phase 6 named one) | 200 | — |
| `tests/pdfium/images.py` | PDFium's decoded bitmap of each image object of an output (`FPDFImageObj_GetBitmap`, recalled; verify against the pinned headers), compared inside and outside the treated rectangle | 120 | PDFium `fpdf_edit.h` (design record Appendix A, stream 1, read) |
| `tests/dataset/gates/phase9.py` | the gate of §4 | 100 | — |

## 3. Tests

### 3.1 Unit tier (`tests/unit/treatments/`), fixtures written in code

| File | Asserts |
|---|---|
| `box_painter_test.cpp` | a box in DeviceGray, DeviceRGB, DeviceCMYK, `Indexed` and a `/Decode [1 0]` image: every sample inside is the fill, every sample outside unchanged; a box partly outside the image clipped |
| `caption_drawer_test.cpp` | the caption fits and is centred; an alphabet violation refused with the three-part message; the bitmap data's hash equals the generator's |
| `mosaic_blur_test.cpp` | every block constant; the block size rule; the bitonal refusal below the threshold names `box` |
| `region_mapper_test.cpp` | a page-space region under an identity, a scaled, a rotated and a flipped image matrix maps to the expected pixel rectangle |
| `reencoder_test.cpp` | Flate round trip at 1, 2, 4, 8 and 16 bits per component; JPEG re-encode of a DCT source decodes with the phase 6 decoder to samples equal to the painted buffer within the JPEG tolerance outside the box and exactly the fill inside it after a dilation of one 8-pixel block; a bitonal source becomes 1-bit Flate with `/Decode` meaning kept |
| `soft_mask_treatment_test.cpp` | the soft mask under a box is opaque inside the rectangle; under a blur it is mosaicked; a colour-key mask range does not punch a hole in the box |
| `image_remover_test.cpp` | an XObject drawn on two pages and inside a form: every `Do` gone, `q`/`Q` balanced, the object absent from the output; an inline image removed; the encoded bytes of a removed image found nowhere in the output |
| `image_treatment_applier_test.cpp` | one treatment reaches every occurrence; the write report's rectangles equal the treated occurrences' device boxes; text and other images on the page untouched (their records equal before and after) |
| `api_redact_images_test.cpp` | the symbol through the C ABI: each refusal's three fields; a second treatment of one identity refused |

### 3.2 Dataset

- Tags this phase checks are phase 6's image tags (per encoding, per colour
  space, soft mask, stencil, inline, shared across pages, in a form); this
  phase adds `treat:region` and `treat:caption` (minimum one file each) from
  the synthesiser and raises the minimum of `image:shared` and `image:in_form`
  to five.
- The manifest column `redact_images` (`clean`, `known_gap:<reason>`, `n/a`
  for a file with no image).
- `tests/dataset/run.py` gains the `redact_images` check: per file, a seeded
  job (seed `9` plus the file's id) that gives each image identity a treatment
  in the cycle `box` (whole image, with the caption `REDACTED` on every second
  box), `blur`, `remove`, and one region box per file at a seeded rectangle;
  undecodable images get `remove`.

### 3.3 PDFium comparison

`tests/pdfium/images.py` decodes every treated image object of each output and
checks the interior of each treated rectangle; `tests/pdfium/compare.py
--check redact` (from phase 8) compares page renderings outside the treated
occurrences' device rectangles dilated by one pixel; `tests/pdfium/leak.py`
checks that the encoded bytes of every removed and every treated image's
original stream occur nowhere in the output.

## 4. Gate

Over every manifest file whose `redact_images` expectation is not `n/a`:

| Measure | Threshold |
|---|---|
| treated rectangles whose pixels, decoded by PDFium from the output, carry an original sample (box: not the uniform fill; blur: not block-constant) | 0 |
| removed identities with an occurrence left in the output | 0 |
| removed or treated identities whose original encoded bytes occur anywhere in the output | 0 |
| pages whose PDFium rendering differs from the input only inside the treated occurrences' rectangles (one-pixel dilation) | at least 99% |
| outputs that pass the self-check; text agreement of the output with the input (pdfkit and PDFium) | 100%; unchanged from phase 8's run |
| two runs: output bytes identical | 100% |
| crashes, hangs past 30 s, anything but a status from the ABI | 0 |

Commands:

```
cmake --preset mingw-release && cmake --build --preset mingw-release
python tests/dataset/run.py --checks open,redact_images --runs 2
python tests/pdfium/images.py --run <run-id>
python tests/pdfium/compare.py --run <run-id> --check redact
python tests/pdfium/leak.py --run <run-id> --images
python tests/dataset/gates/phase9.py --run <run-id>
```

The report goes to `dataset/reports/<run-id>/`; the summary goes into the merge
commit message.

## 5. Kill criterion and the question to the owner

**Survival (stop the line).** **Quantity**: treated rectangles with an original
sample, removed identities with an occurrence or their bytes left, in any
output. **Threshold**: one. **The question**: "Original image pixels or bytes
survived treatment in N files via <cause, with counts>. Do you want:

- (a) a fix session on that cause before anything else (recommended): about 3
  to 6 hours and $50 to $100;
- (b) continue with the defect recorded and no release until it is fixed: no
  extra cost now."

**Unattended action**: the session fixes the cause itself when it can within
this phase and re-runs the gate. When it cannot, it writes the question with
the evidence into `OPEN-QUESTIONS.md`, pushes the branch with trailer
`Build-Phase: 9 incomplete` and the reason, and **does not ask for a merge**,
because `main` must never hold a treatment that leaves the original pixels.

**Fidelity.** **Quantity**: pages pixel-stable outside the treated rectangles.
**Threshold**: under 97%. **The question**: "Image treatment keeps N of M pages
pixel-stable outside the treated rectangles; the others differ because of
<named causes>. Do you want (a) one more session on those causes
(recommended): about 4 to 6 hours and $70 to $100; (b) accept and record them
as known gaps: no extra cost; (c) a half-day examination, then decide: about 3
hours and $50?" **Unattended action**: write the question with the evidence
into `OPEN-QUESTIONS.md`; merge the branch if checks, the unit tier and the
survival measures (zero) are green, with trailer
`Build-Phase: 9 gate-failed`; phase 10 proceeds and reports its gates with that
caveat.

## 6. Rollback

Revert the merge: the treatments, the image half of the edit plan, the bitmap
font data and the `images` section of the job file leave together, and the
job file's refusal sentence for that section returns with them. Phases 7 and 8
keep working, because the image-treatment stage of the pipeline returns to its
no-op form.

## 7. Execution order inside the phase

1. Run `python build/check.py` and `ctest --preset unit` on the base (which
   holds phase 8); a red base is repaired in a commit of its own and recorded
   in §9.
2. `build/fonts/bitmap_font.py` and its regeneration check.
3. `PixelBuffer`, `BoxPainter`, `CaptionDrawer`, `MosaicBlur`,
   `RegionMapper`, `Reencoder`, `SoftMaskTreatment`, each with its failing
   test first.
4. **Split point**: if the session is running out of room, push here with
   `Build-Phase: 9 incomplete` and the reason; the next session starts at 5.
5. `ImageTreatmentPlan`, `ImageRemover`, `ImageTreatmentApplier`, the
   edit-plan extension, the pipeline stage, `src/api/redact_images.cpp`, the
   job file's `images` section.
6. The synthesised image files, their manifest rows and tags,
   `tests/pdfium/images.py`, the dataset check, `phase9.py`; run the gate;
   read every failure and classify it; a survival is fixed here (§5).
7. The test sweep: `python build/check.py`, `ctest --preset unit`,
   `ctest --preset sanitize`, the gate commands of §4.
8. The documentation sweep, last, from what landed: `docs/ABI.md` (the symbol,
   the job file's images section), `docs/DESIGN.md` §14 only where the landed
   code differs (each difference a deviation in §9), `docs/SOURCES.md`,
   `dataset/README.md` (the column), `THIRD-PARTY-NOTICES.md` unchanged (the
   bitmap font derives from the already-credited Liberation Sans; say so in the
   merge message).

## 8. Opening prompt

> Read `CLAUDE.md`, `docs/DESIGN.md` chapters 9, 14, 15, 17, 18 and 19,
> `docs/ABI.md`, `dataset/README.md`, all of `build-9.md`, and the deviations
> sections (§9) of `build-5.md` to `build-8.md`, then implement `build-9.md`
> exactly as written. Before planning, read as landed `src/images/` (identity,
> decoders, colour spaces, masks, the PNG writer), `src/text/`'s image
> occurrences, `src/redact/` (the edit plan as phase 8 left it, the
> self-check), `src/redact/write_pipeline.*`, `build/fonts/` and
> `tests/pdfium/`. Work in a worktree of your own on branch
> `phase/9-redact-images` created from `main` after phase 8 has merged. Run
> `python build/check.py` and `ctest --preset unit` before you start and before
> you finish; a red base is repaired in a commit of its own and recorded as a
> deviation. Every external fact you implement carries its source and whether
> you read it there or recalled it, in the comment and in `docs/SOURCES.md`.
> The dataset and the PDFium binary live in the main checkout (resolve it with
> `git rev-parse --git-common-dir`). No step waits for the owner: a question
> goes into `OPEN-QUESTIONS.md` with the recommended answer first and the price
> of each option. Write failing tests first. An original pixel or byte that
> survives a treatment is fixed in this phase or the branch is pushed
> `incomplete` and not offered for merge. An overlay drawn over an image is
> never a treatment. Finish with a last commit whose message carries
> `Build-Phase: 9 done`, or `Build-Phase: 9 incomplete` or
> `Build-Phase: 9 gate-failed` with the reason, and push the branch; never
> merge into `main`, because the team lead merges. Do not write metadata:
> phase 10 owns it. If you must deviate from this file, record each deviation
> in its §9 and in `build-10.md`, in the same change.

## 9. Deviations recorded by this phase

1. (none yet)

### Deviations of earlier phases that affect this one

From `build-5.md` §9:

- Image occurrences are `text::PageContent::images` from `text::TextReader::interpret(page)`: `ctm` (the unit square under it is the image's place; its inverse maps a page-space rectangle into image space), `box`, `form_path`, and `stream`/`operation` naming the `Do` or `BI` to remove with an edit of `text::ContentSerialiser::write` (empty bytes remove the operation). An inline image's `data_start`/`data_end` name its data in the walk's bytes.
- One form drawn twice is two walks with the same `ContentStream::form`; a removal inside it is made once in the form's bytes.

From `build-1.md` §9:

- The redact job's `images` section is forwarded byte for byte to `pdfkit_redact_images` (`docs/ABI.md` §6, item 7).
- The sanitizer tier is two runs: `ctest --preset sanitize` (GCC, UBSan in trap mode) and `ctest --preset clang-sanitize` (llvm-mingw, ASan and UBSan) (`build-1.md` §9 item 8).

From `build-7.md` §9:

- The write surface is `writer::ObjectGraph` (`src/writer/object_graph.hpp`): `get(number)` reads an object with every earlier stage's edits applied, `set(number, object)` replaces one, `add(object)` adds one under a fresh number; the source `syntax::Document` is never changed. Read through the graph, never through `Document::object`, because page selection runs first and replaces page dictionaries, the catalog and every pruned form (a form with resources of its own gets a pruned copy under its own number) (items 2, 5).
- A created stream is `syntax::Stream(dictionary, encoded bytes)`; `writer::StreamCopier::compress(dictionary, decoded)` makes a Flate one, `StreamCopier::plain` an uncompressed one; the file writer copies untouched streams byte for byte (decrypted, `/Crypt` dropped) (`build-3.md` §9 item 17).
- `redact::WritePipeline` (`src/redact/write_pipeline.cpp`) has the named stages page selection, text surgery, image treatments, metadata, channel stripping, serialisation, self-check; fill your stage, never reorder. The write report (`writer::WriteReport`) already carries `rectangles` (empty) and `self_check` (`not_run` with its reason) (items 2, 10).
- `redact::EditPlan` holds the page list only; add your member beside it (its header names where).
- Every write prunes resources to the names the content uses (`writer::ResourceUsage` reads `Tf`, `Do`, `gs`, `cs`/`CS`, `scn`/`SCN`, `sh`, `BDC`/`DP` and inline-image colour spaces), so an object your stage stops drawing leaves the output without further work, and a resource your stage starts drawing must be named in the resources it edits (item 5).
- The channel table strips after your stage: `/Metadata` off every object but the catalog, the catalog's XMP loses its whole `xmpMM` namespace, `/PieceInfo`, `/LastModified`, `/AA`, `/JS`, `/AF`, `/EF`, `/StructParent(s)` go from every dictionary, and the catalog and pages keep only their allow-lists (items 7, 11, 12).
- Test helpers: `tests/unit/writer/output_scan.hpp` (`identity_write`, `select_write`, `write_report`, `everything` = raw bytes plus every stream decoded, `contains_marker`, `reopen` without repair) and `tests/unit/support/pdf_builder_channels.hpp` (planted markers per channel, the four-page leak fixture); `tests/pdfium/render.py` compares pages exactly or outside a rectangle set with a one-pixel dilation (`compare_bitmaps`).
- The dataset outcome gains `channels`, `write` and `select`; outputs go to `<run>/outputs/`, named relative to the run folder (`tests/dataset/outcome.md`).
- An image treatment replaces the image object through `ObjectGraph::set` with a created stream; a removed image's `Do` goes from the content streams that draw it, and pruning then drops the XObject entry.

From `build-6.md` §9:

- The decoded image is `images::PixelImage` (`src/images/pixel_image.hpp`): grey or RGB samples at 1 bit (one byte per sample, 0 or 255), 8 or 16 bits (most significant byte first), a CMYK source's 8-bit CMYK samples kept in `cmyk`, an optional 8-bit `alpha` plane and a stencil's `transparent` grey; `images::ImageDecoder::decode(document, stream, Part::base | Part::mask)` returns it or a fault (`undecodable_image`), and `images::PngWriter::write` writes it (items 10, 11).
- Images are found by identity through `images::ImageCatalogue` (`src/images/image_catalogue.hpp`), which the document handle keeps (`pdfkit_document::images`); its entry holds the stream and its object number (item 2).
- CMYK converts through `images::AdobeCmyk` (PDFium's table), not the naive complement; re-encode a treated CMYK JPEG from `PixelImage::cmyk` (item 5, `OPEN-QUESTIONS.md` §2.7).
- A CMYK JPEG's samples are used as libjpeg returns them, never inverted for an Adobe marker (item 6).
- libjpeg-turbo's compressor is linked into `pdfkit_core` (public), so re-encoding needs no new dependency; `tests/unit/images/image_fixture.hpp` has `encode_jpeg` for test fixtures (items 11, 13).
- Images by identity with their draws: `images::ImageListing` (`src/images/image_listing.hpp`), kept on the handle as `pdfkit_document::listing`; its occurrences carry page, ordinal, box and form path but not the stream walk and operator index, so a removal reads `text::ImageOccurrence` from `TextReader::interpret` itself (`build-6.md` §9 item 16).

From `build-8.md` §9:

- The edit plan's text part is `redact::TextPlan` (`EditPlan::text()`), filled by `redact::TextRedactor::add` and applied by `TextRedactor::apply` in the text surgery stage, before your stage; add your member to `EditPlan` beside it. The self-check runs on every write whose plan holds a text match (`WritePipeline::self_check`, `redact::SelfCheck`); a plan of image treatments alone leaves it `not_run` until you decide otherwise, and its raw scan skips image samples, so a treated image never refuses an output (items 2, 8).
- `writer::WriteReport::rectangles` is a list of `{page, source_page, boxes, placeholders}` per output page (`docs/ABI.md` §4.7); add the treated rectangles to `boxes` of the same entries so `tests/pdfium/compare.py --check redact` (which reads the outcome's `rectangles` as `[page, left, bottom, right, top]` rows) judges both (item 2).
- The dataset outcome's text member is `redact_text` (after `select`), judged by `expect_redact_text` (`clean`, `known_gap:<reason>`, `n/a`), the column before `notes`; name yours `redact_images` / `expect_redact_images` the same way, before `notes`. Phase 2's `expect_redact` (`stable`, all `n/a`) is unused: use it or retire it in your change (item 10).
- `tests/pdfium/leak.py` reads `redact_text`'s `strings` and `codes`; its `--images` flag is yours to add. It already skips image samples, ICC profiles and font programs, and ignores a hit inside a name token (item 8).
- A new channel row `ch_xobject_private` keeps only the standard keys of form and image XObjects (ISO 32000-1 tables 5, 89, 95, 146), so a re-encoded image's dictionary keeps `/Matte`, `/SMask`, `/Decode`, `/ColorSpace` and drops anything private; `tests/dataset/gates/phase7.py` counts rows 01 to 24 (item 9).
- Liberation Sans Regular and Bold are vendored in `third_party/liberation-fonts/` (TTF files, OFL licence, inventory row); `build/fonts/subset.py` shows the generator pattern (fontTools pinned in `build/fonts/requirements.txt`, the output registered in `build/check_generated.py`); the alphabet check is `writer::PlaceholderFont::in_alphabet` / `outside` and `redact::PlaceholderFitting::require_alphabet` gives the three-part refusal (items 2, 3).
- The redact verb refuses an `images` section with a usage error while `pdfkit_redact_images` is not implemented (`is_implemented` in `cli/support.cpp`); it lifts by itself once the catalogue row flips (item 12).
