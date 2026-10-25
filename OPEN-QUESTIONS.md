# Open questions for the owner

Working document. Each question is the owner's to answer and is written in
plain words, with the recommended answer first and the price of each option in
hours and model spend. Work that does not depend on the answer continues. When a
question is answered, its answer moves into the rule it decides
(`CLAUDE.md`, `docs/DESIGN.md` or a build file) and the question is deleted
here.

## 1. Author decisions to confirm or flip

The design took these decisions on the owner's behalf. Each one is cheap to
flip before the phase that implements it and expensive after.

Each line names the rule in `docs/DESIGN.md`; flipping one is a one-line change
to that chapter before its phase starts, and costs nothing beyond that.

- **Optional content keeps hidden layers hidden** (§15). `/OCProperties` is kept
  in minimal form: groups are renamed `OC1`..`OCn`, and the ON/OFF state and
  `/Order` are kept. Stripping it entirely would make every switched-off layer
  visible in the output. The alternative is to strip it and accept that.
- **Catalog and page keys are kept by allow-list** (§15), so a channel nobody
  listed is dropped rather than copied.
- **Digital signatures do not survive** (§15): `/Perms` and `/Legal` are
  stripped. Any edit invalidates a signature anyway.
- **A shared form XObject is rewritten once** (§12), so a redaction inside a
  letterhead reaches every page that draws it.
- **Overlapping matches with different placeholders**: the earlier-starting
  match wins the overlap (§11).
- **Marked-content `/ActualText`, `/Alt` and `/E`** covering a removed glyph are
  rebuilt with the placeholder in the removed part (§12).
- **A treated JPEG is re-encoded at quality 90** (§14).
- **Blur is refused for bitonal images under 128 px** on the smaller side, and
  box is offered instead (§14).
- **The writer writes no object streams** (§17), which costs some output size
  and buys byte-for-byte copying of untouched objects.
- **Output identifiers** (`/ID`, XMP DocumentID and InstanceID) are the SHA-256
  of the output, written with a zero placeholder first and patched in place
  (§10, §17).
- **Limits** (§3): a page ceiling of 10,000 pages, 8,388,607 objects and 50
  million content operators per page, all configurable at open.
- **A missing `/MediaBox` defaults to US Letter** (§4).
- **Right-to-left lines** keep the producer's drawing order unless analysis of
  strong characters says the line was drawn in visual order (§7).

## 2. Questions raised by build phases

*(each phase appends here when its kill criterion trips or a fact it needs is
the owner's)*

### 2.1 AddressSanitizer through a second toolchain (raised by phase 1)

The pinned MinGW GCC supports UBSan (trap mode) but not AddressSanitizer on
Windows: `-fsanitize=address` fails with `cannot find -lasan` (and likewise
`-lubsan`, `-ltsan`, `-llsan` without trap mode). llvm-mingw 20261020 (clang
23.1.4, a 190 MB zip, no administrator rights) was measured on the laptop: ASan
reports a heap overflow with its source line and UBSan works with its runtime.
Do you want llvm-mingw added as a second toolchain for the sanitizer tier only?

- **(a) Yes — recommended, and taken while this question stands.** It is
  already in place: the `clang-sanitize` preset, `build/llvm-mingw.cmake`, the
  pin and fetch command in `build/toolchain.md`, the zip unpacked in the main
  checkout's `build/cache/llvm-mingw/`. The unit tier passes under it. Price
  spent: about 1 hour and $15 to $25 of model spend; each later phase's
  sanitizer run takes about 2 more minutes. GCC stays the compiler of every
  artefact.
- **(b) No:** the sanitizer tier stays UBSan-only and memory errors are caught
  by the bounds-checked helpers and the dataset's crash count. Price: about 15
  minutes and $3 to $5 to remove the preset, the toolchain file and its
  section of `build/toolchain.md`; the cache folder can be deleted by hand.

### 2.2 Word and Edge cannot be driven unattended on the laptop (phase 2)

`dataset/tools/generate_office.ps1` produced the Excel and PowerPoint files
(Save as PDF and Microsoft Print to PDF, 4 files), but no Word and no Edge file:
Word's automation server never answers (the COM call fails after more than 120 s
with `CO_E_SERVER_EXEC_FAILURE`; the Word-only add-ins `MSIP.WordAddin` and
`PwC.SmartWord` are the likely cause), and Edge headless writes no PDF within
90 s on the managed profile with its forced extensions. So the taxonomy tags
`gen_word_pdfa` and `gen_edge` stay at 0 of their minimum of 1. The script stays
able to drive both; nothing on the laptop was changed to make them work.

- **(a) Recommended: run the script once while you are at the laptop**,
  `powershell -ExecutionPolicy Bypass -File dataset/tools/generate_office.ps1 -Only word,edge`,
  and answer whatever Word or Edge shows. About 10 minutes of your time, then
  about 0.5 hour and $5 to $10 of session time to check and commit the 4 new
  rows.
- **(b) Accept the two tags under minimum.** No cost now; Word's own Save as PDF
  is still covered by public files (`gen_word` is above its minimum from the
  wild sample), but Word's PDF/A output and Edge's printing stay untested until
  someone runs (a), at the latest in phase 10.
- **(c) Ask IT to let Word start for automation without the two add-ins.**
  Unknown delay on IT's side; then (a) at the same cost.

The session took (b) for now; the gate counts 121 of 123 tags at minimum.

### 2.3 corkami's proofs of concept carry no licence (phase 2)

`build-2.md` planned 15 files from https://github.com/corkami/pocs as
`terms:none`, local reading only. The repository has no LICENSE file and its
README states no terms, so under the rule "admit a set only when its licence or
terms allow local testing" the session recorded it `licence_unclear` and did
not fetch it; the trap patterns it would have covered are synthesised instead.

- **(a) Recommended: keep it refused.** No cost.
- **(b) Admit it as `terms:none` for local reading only.** About 0.5 hour and
  $5 to $10 of session time to fetch, select and register 15 files; the files
  would then sit in the dataset with no grant of any right.

### 2.4 A revision 6 /Perms that fails its check (raised by phase 3)

`docs/DESIGN.md` chapter 2 says a `/Perms` mismatch is a warning, never a
refusal. The dataset's `pdfium-0012` (PDFium's `bug_424613308.pdf`) shows the
two kinds of mismatch differ: its `/UE` is corrupt, so the empty password
matches `/U` but the file key decrypted from `/UE` is wrong, `/Perms` does not
decrypt to "adb", and every string and stream would decrypt to noise. PDFium
refuses it as a password failure. Phase 3 refuses a `/Perms` without "adb" as
`password_required` (the message names `/Perms`) and keeps the warning for a
`/Perms` that is missing or names other permissions than `/P`. Do you want that
rule written into chapter 2?

- **(a) Yes, refuse when "adb" fails — recommended, and taken while this
  question stands.** A file key that fails its own check would give the caller
  noise read as text; refusing names the damage. Price: a two-line change to
  chapter 2, about 10 minutes and $2 to $4.
- **(b) No, warn and open as chapter 2 says.** The file opens and every page's
  text is noise with a `repaired` warning. Price: about 20 minutes and $4 to $6
  to revert the check and its test, and `pdfium-0012`'s `refused:password_required`
  expectation then fails in every dataset run.

### 2.5 Glyph names that only restate their code (raised by phase 4)

`docs/DESIGN.md` §5.2 reads the names `Cdd`, `Cxx`, `gNN` and `cidNN` only
through the font program's own map, because they name code or glyph numbers,
not characters; without a program, which is every Type 3 font, the code is
unreadable and counted. The font census found 17 files tagged
`differences_cdd` (median character F1 0.0 against PDFium) whose Type 3 fonts
name each glyph after its own code: `/Differences [65 /C41 /C42 ...]` in
pdfjs-0047 and the mutated files made from it, the code 65 drawing an "A".
PDFium reads them because it falls back to the code itself when no map
answers; pdf.js reads `Cdd` as the decimal character and `Cxx` as the
hexadecimal one. The census gate passes either way (median F1 1.0, 0.54%
unreadable). Do you want such names read as text?

- **(a) Keep them unreadable, as the design says — recommended, and kept
  while this question stands.** A name that restates its code says nothing
  about the glyph, and an unreadable code is counted and named in the
  `unreadable_text` warning, while a guessed character is silent. No cost.
- **(b) Read a `Cdd` or `Cxx` name whose number equals its own code as the
  code's WinAnsi character, in a font with no program.** The 17 files would
  read as PDFium reads them; a producer that numbers glyphs by any other
  scheme would read wrong text in silence. About 1 to 2 hours and $15 to $30
  (the rule, two unit cases, `docs/DESIGN.md` §5.2 and a census run).

### 2.6 Every write prunes the resources nothing draws (raised by phase 7)

`docs/DESIGN.md` §16 asked for resource pruning when pages are selected. The
writer prunes on every write, a write that keeps every page included: each
page's resource dictionary, and each form's, keeps only the names its content
stream uses (plus `/DefaultGray`, `/DefaultRGB`, `/DefaultCMYK` and
`/ProcSet`), so a font, image or form that a shared `/Resources` lists but no
page draws is not written. An undrawn form can hold text an extractor reads,
so it is a channel; the price is fidelity risk where a content stream uses a
name the usage reader misses, which the gate measured at 0 pages of 8,027
(every differing page has another cause, `build-7.md` §9 item 14). §16 now
says so. Do you want to keep it?

- **(a) Keep pruning on every write — recommended, and taken while this
  question stands.** No cost.
- **(b) Prune only when pages are selected.** About 0.5 hour and $5 to $10
  of session time (one condition in `src/writer/page_selector.cpp`, its test,
  §16 and a gate run); undrawn resources then reach every identity write.

### 2.7 CMYK images convert through PDFium's table, not the naive complement (raised by phase 6)

`docs/DESIGN.md` §9.4 decided, as an author decision, that CMYK becomes RGB by
the naive complement `R = (1-C)(1-K)`. Measured against PDFium over the
dataset, that complement differs by a mean of 7 to 40 per channel on 93 of the
107 comparable DeviceCMYK images and on every Separation and DeviceN image with
a CMYK alternate, which alone puts the continuous-tone gate (MAD at most 2 on
99% of images) out of reach (about 96%). PDFium converts through Adobe's CMYK
table: a 9 x 9 x 9 x 9 grid of sRGB samples interpolated per axis, about 20 KB
of data under PDFium's BSD-3 licence. The phase ported it
(`src/images/adobe_cmyk.cpp`, notice in `THIRD-PARTY-NOTICES.md` §8) and every
DeviceCMYK, Separation and DeviceN image now agrees with PDFium (the gate
reads 99.08%). Do you want to keep it?

- **(a) Keep PDFium's Adobe CMYK table — recommended, and taken while this
  question stands.** No cost; a CMYK image then looks as it does in Chrome's
  viewer, and the treated images of `build-9.md` re-encode from the kept CMYK
  samples either way.
- **(b) Return to the naive complement.** About 0.5 hour and $5 to $10 (one
  function, its tests, the notice row and a gate run); the continuous-tone
  gate then fails at about 96% and CMYK images look washed out beside every
  viewer.

### 2.8 ICC profiles are read as their device space (raised by phase 6)

No colour management is vendored, so an `ICCBased` image converts as the
device space of its component count. PDFium applies the embedded profile
through Little CMS, so 17 `ICCBased` CMYK images (all in one file, dccc-0065)
and 10 `ICCBased` RGB and grey images differ from PDFium by a mean of 3 to 30
per channel. The gate passes without them (99.08% of 3,697 images). Do you
want colour management?

- **(a) No — recommended, and taken while this question stands.** No cost;
  the listed images keep a visible but moderate colour shift, and the
  `ICCBased` disagreements stay named in the gate report.
- **(b) Vendor Little CMS 2 (MIT) and convert through each image's profile to
  sRGB.** About 6 to 10 hours and $100 to $180 (vendoring, the profile cache
  per document, the budget on profile sizes, tests, a gate run); about 300 KB
  more in `pdfkit.dll`; the CMYK table of §2.7 then serves only images without
  a profile.

### 2.9 The per-document decoding budget spans every image call (raised by phase 6)

`docs/DESIGN.md` §3.2 counts every decoded byte of a document handle against
2 GiB. `pdfkit_image_data` decodes on each call, so a caller extracting every
image of a large scanned document (hundreds of page-sized images) reaches
`limit_exceeded` on the handle after about 2 GiB of pixels, although each image
is far below the 512 MiB per-stream limit. The images layer leaves the rule as
it is. Do you want it changed?

- **(a) Keep one budget per handle — recommended, and taken while this
  question stands.** No cost; a caller that needs more opens the document with
  a larger `document_bytes` or reopens it, and a hostile file still cannot make
  one handle decode more than the limit.
- **(b) Count each image call against a fresh per-call budget of the same
  size.** About 1 hour and $15 to $25 (a budget scope in `syntax::Document`,
  shared with phase 3's code, tests); one handle can then decode without
  bound over many calls.

### 2.10 The reading gate misses its share below 0.90 (raised by phase 5)

pdfkit agrees with PDFium on 819 of 875 files at the gate; the files below it
fail on Type 3 glyph names that restate their codes (6, §2.5), CJK fonts with
no CMap data (6) or with estimated widths (2), unreadable subset fonts (3),
four layout faults (two lines merged, a vertical word split, a fold in
mirrored text, a math font's unreadable codes), PDFium misreading the file
(27) and legitimate differences (8: a duplicate fold, a self-drawing form, a
missing font, scripts without spaces, a damaged stream); do you want (a) one
more fonts/layout session on those causes (~8-12 h, ~$150-250), (b) accept
the reader as is and continue to redaction, recording the named files as
known gaps, or (c) stop and re-plan?

The numbers, from the run of 2026-10-25 over the 875 files both readers open
(`agreement.json` and `summary-text.md` in the run folder
`phase5-final` of the main checkout's dataset reports): median F1 **1.0**
(gate 0.97, passed); **56** files below 0.90, **6.4 %** (gate 5 %, failed);
over the 755 files where either reader finds words, median 1.0 and 7.4 %
below. By cause (`tests/pdfium/text_causes.tsv` gives each file its line of
evidence): **pdfkit 21** (2.4 % of the files), **PDFium 27**, **legitimate
8**. Every other item of the gate passed: the re-serialisation identity on
12,072 of 12,072 content stream walks (the 87 damaged walks identical too),
two runs byte-identical on every file, every planted hidden marker of the 24
synthesised hidden-text files extracted and flagged with its class, no hang
and no crash.

The twenty worst files (F1 0.0 each):

| Files | Cause | Evidence |
|---|---|---|
| mutate-0008, mutate-0011, mutate-0018, mutate-0023, mutate-0028, pdfjs-0047 | pdfkit | Type 3 `Cdd` names read as unreadable while §2.5 stands; PDFium reads the code as a character |
| synth-0195 to synth-0204, synth-0209 | PDFium | pdfkit reads the specimen's stated text through the font program; PDFium reads digits or nothing |
| safedocs-0007, safedocs-0008, safedocs-0010 | PDFium | Type 3 glyphs that draw shapes, which PDFium emits as "a" and "b" |

The PDFium-caused files count against the gate as it is written ("at most one
file in twenty below 0.90", whatever the cause).

- **(b) Accept the reader as is and continue — recommended, and taken while
  this question stands** (the branch carries `Build-Phase: 5 gate-failed`;
  the named files are `known_gap:<cause>` in the manifest). The pdfkit-caused
  share is 2.4 %, most of it a decision already on this list (§2.5) or CJK
  data that ships later, and phases 8 and 9 rely on the text model existing,
  not on the agreement number. No cost now. Consider also amending the gate
  of `build-5.md` §4 to count only `pdfkit`-caused files, which this run
  passes.
- **(a) One more fonts/layout session on those causes** (~8-12 h,
  ~$150-250): the Adobe CMap resources (BSD-3, 6.79 MB, named in
  `docs/SOURCES.md`), a half-width default for the Latin codes of a
  non-embedded CJK font, §2.5 (b), and the four layout faults. The 27
  PDFium-caused files stay below 0.90 whatever pdfkit does, so (a) passes the
  gate as written only if it fixes at least 13 of the 21 pdfkit-caused files.
- **(c) Stop and re-plan.** The cost of re-planning; nothing in the numbers
  points to a reader that does not work.

### 2.11 Text redaction: what the search folds and where the self-check looks (raised by phase 8)

Phase 8 took these five decisions unattended, each the recommended answer
and each taken while this question stands; `build-8.md` §9 records them and
`docs/DESIGN.md` §11, §12, §15 and §18 say so. Do you confirm them?

1. **No compatibility folding.** The default folding is the one §11 names
   (simple case folding, canonical decomposition with the marks stripped,
   white space collapsed), so a search for "HDCP" leaves a full-width
   "ＨＤＣＰ" and a search for "essbar" leaves "meßbar". The first gate run's
   three PDFium "leaks" were exactly these (dccc-0025, dccc-0072, dccc-0004),
   and the leak check now folds as pdfkit does.
   - **(a) Keep it, and add an opt-in `compatibility` folding flag later —
     recommended.** About 2 to 3 hours and $40 to $60 when wanted (NFKC data
     vendored beside `third_party/unicode/`, the full case folding of
     `CaseFolding.txt` status F, one flag in `docs/ABI.md` §4.11).
   - (b) Make compatibility folding the default. About 3 to 4 hours and $60
     to $80; more text goes than the caller named (a superscript digit, a
     ligature, ß in every German word holding "ss").
   - (c) Keep it with no flag. No cost.
2. **A search matches inside words** ("Smith" in "Smithson"), because a
   redaction that misses a string inside a longer word leaks it.
   - **(a) Keep it — recommended.** No cost.
   - (b) Add an opt-in whole-word flag. About 2 hours and $30.
3. **The private keys of form and image XObjects are a channel** (the new
   row `ch_xobject_private`, an allow-list of ISO 32000-1 tables 5, 89, 95
   and 146). The self-check found a pdfTeX `/PTEX.FileName` and PDFpen's
   private copy of an annotation's text on forms that every write kept.
   - **(a) Keep the allow-list — recommended.** No cost.
   - (b) Strip only the keys a test names (a deny-list). About 1 hour and
     $20, and the next unknown key leaks.
4. **The self-check's raw scan of a content stream reads its string
   operands, comments, the bytes a lexer recovery skipped and unknown
   operators**, not operator and resource names, so a tagged page's `/Table`
   or `/Figure` does not refuse a redaction of "Table" or "Figure". **For a
   plan of search strings alone it reads the text of the pages the redaction
   changed** again, not every page: every other page was searched with every
   string by the same reader. This keeps the 910-page dccc-0135 inside the
   30 s dataset limit (22 s alone before, 11 to 12 s now).
   - **(a) Keep both — recommended.** No cost.
   - (b) Read every page again. No development cost; about 40 % more write
     time on large files, and dccc-0135 past 30 s under load.
   - (c) Scan whole content streams. No development cost; refusals on
     common words in tagged files.
5. **A redacted word that is also in the Info dictionary or the XMP packet
   refuses the output**, as the self-check must, so 85 dataset rows are
   `known_gap:metadata_holds_word` until the dataset job can rewrite
   metadata.
   - **(a) Let phase 10's dataset job rewrite the fields that hold a redacted
     word and turn those rows `clean` — recommended.** About 1 hour inside
     phase 10, $15.
   - (b) Keep them as gaps. No cost.
