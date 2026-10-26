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

`docs/archive/build/build-2.md` planned 15 files from https://github.com/corkami/pocs as
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
(every differing page has another cause, `docs/archive/build/build-7.md` §9 item 14). §16 now
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
  viewer, and the treated images of `docs/archive/build/build-9.md` re-encode from the kept CMYK
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
and each taken while this question stands; `docs/archive/build/build-8.md` §9 records them and
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

### 2.12 Metadata rewriting: what goes with a rewrite, what the self-check skips, the dataset's time limit (raised by phase 10)

Phase 10 took these decisions unattended, each the recommended answer and
each taken while this question stands; `build-10.md` §9 records them and
`docs/DESIGN.md` §10, §15 and §18 say so. Do you confirm them?

1. **A value outside the field list that repeats a redacted string is
   removed.** When a write redacts a string (an original value of a rewritten
   field, or a string the text redaction removes), every custom Info entry,
   every XMP property no field pairs with (`xmp:MetadataDate`, `dc:rights`,
   `pdfx:Identifier`, `photoshop:headline` and the like) and the packet's
   toolkit attribute `x:xmptk` that holds it, under the default folding, goes
   from the output; every other custom key and property stays byte for byte.
   The first dataset run refused 296 of 802 metadata rewrites for such
   repeats (the original modification date in `xmp:MetadataDate`, an author's
   name in `pdfx:Identifier`), and no catalogue function can rewrite those
   values, so without this the caller has no way to an output.
   - **(a) Keep the targeted removal — recommended.** No cost.
   - (b) Strip every custom Info key and every XMP property outside the
     field list on every write (an allow-list, as the catalog and pages
     have). About 2 hours and $30; PDF/A and PDF/UA identification
     (`pdfaid:`, `pdfuaid:`) would need an exception, and a document loses
     custom properties no redaction touched.
   - (c) Keep custom values and let the self-check refuse. No cost; 296 of
     the dataset's rewrites, and any real document with such a property,
     cannot be written.
2. **An original value of fewer than three letters or digits is not
   searched for** by the self-check ("A.B", "PC", "1"), because it occurs in
   almost any text and would refuse every output rather than find a leak.
   - **(a) Keep three — recommended.** No cost.
   - (b) Search every original, however short. No development cost; such a
     rewrite is refused whenever the page text holds the letters.
   - (c) Raise the threshold to four. No development cost; a three-letter
     name ("Ann", "Bob") is then not proven gone.
3. **The XMP identifiers stay, regenerated.** The channel strip keeps
   `xmpMM:DocumentID` and `xmpMM:InstanceID` only when they hold the
   placeholder the metadata stage writes, and the file writer patches both
   from the output's hash with the `/ID` (`xmp.did:` and `xmp.iid:` plus 32
   hexadecimal digits); every other media-management property goes, whatever
   its prefix (`xapMM:` included, which the strip's literal prefix missed).
   A packet without identifiers gains none.
   - **(a) Keep them — recommended.** No cost.
   - (b) Drop the identifiers with the rest of the namespace, as the writer
     did before this phase. About 0.5 hours and $10.
4. **The dataset tier's per-file timeout grows with the page count**: 30 s
   for each started block of 200 pages (dccc-0135, 910 pages, gets 150 s),
   because the dataset verb now reads every page three times (the text, the
   text redaction and the metadata rewrite); alone, dccc-0135 takes 29 s and
   dccc-0129 16 s, and under four workers the first probe timed both out. A
   file that loops still meets the limit.
   - **(a) Keep the scaled limit — recommended.** No cost.
   - (b) Keep 30 s and merge the text redaction and metadata jobs into one
     edit plan, saving one read. About 2 to 3 hours and $40 to $60; the
     phase 8 gate would then measure a different job, and dccc-0135 stays
     near the limit under load.
   - (c) Keep 30 s and count the large files as hangs. No cost; the hang
     measure of every gate fails.
5. **A packet that states nothing is dropped, and a file without a packet
   gains none.** A rewrite writes the Info dictionary (creating one when
   needed) and the packet the file has; it does not create a packet.
   - **(a) Keep it — recommended.** No cost.
   - (b) Create a minimal packet holding the set fields when the file has
     none. About 1 to 2 hours and $20 to $30.
6. **The ABI version stays 1.** `CLAUDE.md` §3.3 raises `pdfkit_abi_version`
   on every addition, and every phase since the first added JSON fields
   (this one: `xmp_namespaces`, `xmp_history` and `items` in
   `pdfkit_metadata`) while it stayed 1, because nothing has been released
   for a caller to bind to.
   - **(a) Keep 1 for the first release and raise it from then on —
     recommended.** About 0.2 hours to say so in `CLAUDE.md` §3.3 during the
     documentation sweep.
   - (b) Raise it to 2 now. About 0.5 hours and $10 (the constant, the ABI
     tier's expectation, `docs/ABI.md`).
7. **PDFium finds a rewritten field's original in 11 outputs whose page
   text pdfkit reads in another order** (run `p10-gate1`; each row is now a
   named `known_gap:pdfium_reads_*` of `expect_set_metadata`, and the leak
   measure counts none of them). The metadata rewrite job searches the page
   text for every original it rewrites; the search runs over pdfkit's
   reading order, so a multi-word title that pdfkit reads across two
   interleaved columns (dccc-0107, dccc-0139, opf-0010, opf-0018,
   tika-0016), across the lines a non-embedded CJK font runs off the page
   (dccc-0012, dccc-0046; `build-5.md` §9 item 18), in Arabic presentation
   forms where the Info value holds the base letters (pdfbox-0004; the
   compatibility folding of §2.11 item 1), around a right-to-left word
   (pypdf-0019, pypdf-0021) or as the TeX logo's lowered E (qpdf-0038) is
   not matched, and PDFium, reading in its own order, finds it in the
   output. pdfkit's own self-check reads pdfkit's order and passes. This is
   the leak rule of `docs/archive/build/build-8.md` §5 met by a phrase rather than a word; the
   seeded words of the text redaction job show no such survivor (902 files,
   0 PDFium hits).
   - **(a) A fix session on the phrase search before the release —
     recommended**: search a string also over each content stream's drawing
     order (the glyph records as drawn, words joined by their gaps), as the
     code search already does inside one operand, and in the self-check
     likewise. About 4 to 8 hours and $70 to $140.
   - (b) Continue with the 11 rows as known gaps and say in the release
     notes that a phrase pdfkit reads across columns may survive in another
     reader's order. No extra cost now.

   *Taken: (a), on the branch `fix/phrase-search`; its decisions are §2.14.*

### 2.13 Image treatment: what counts as hidden, and what a damaged inline image takes with it (raised by phase 9)

Phase 9 took these four decisions unattended, each the recommended answer
and each taken while this question stands; `docs/archive/build/build-9.md` §9 records them and
`docs/DESIGN.md` §14 says so. Do you confirm them?

1. **A treatment that writes exactly an image's original bytes is not a
   survivor.** A blur of an image whose blocks were already constant (a
   uniform 20 by 20 image in dccc-0120, a test file of large flat colour
   blocks in qpdf-0033) writes the image again byte for byte, so its
   identity, and in qpdf-0033 another image's identical bytes, are still in
   the output. Nothing the blur would hide is there; the dataset job, the
   PDFium image check and the leak search report these as `unchanged`.
   - **(a) Keep it — recommended.** No cost.
   - (b) Make every treatment change the bytes (re-encode with another zlib
     level or add a marker key). About 1 hour and $15; the pixels stay the
     same, so it changes what the check sees, not what a reader sees.
2. **An inline image the content lexer cannot end runs to the end of its
   stream, and a removal takes all of it.** opf-0024 page 14 holds bytes the
   lexer reads as three inline images with no `/Width` and no `EI` it
   accepts; pdfkit's own text stops there in the input already, PDFium reads
   five more lines. The seeded job removes undecodable images, so the output
   loses those lines in PDFium's reading too (`known_gap:unterminated_inline_image`).
   - **(a) Keep it: what pdfkit cannot read it does not keep — recommended.**
     No cost.
   - (b) End such an image at the first white-space-delimited `EI`, as PDFium
     does, and keep the bytes after it. About 2 hours and $30; bytes pdfkit
     cannot read stay in the output, which can hold image data.
3. **The box colour is given as red, green and blue** and painted as the
   sample value of the image's own space that reads nearest (an Indexed
   image's nearest palette entry, a rich black in CMYK), not as component
   values of the image's space, so one job colours every image alike.
   - **(a) Keep it — recommended.** No cost.
   - (b) Also accept component values in the image's space. About 2 hours and
     $30.
4. **A treated inline image is written as its samples under the one filter
   `/AHx`, a JPEG source included**, because binary data can hold the `EI`
   that ends it and one filter is the form every reader of inline images
   handles. An inline image is small (ISO 32000-1 advises under 4 KB), so the
   size costs little.
   - **(a) Keep it — recommended.** No cost.
   - (b) Turn a large treated inline image into an XObject. About 3 hours and
     $45.

### 2.14 The phrase search over every reader's order (raised by the phrase-search fix)

The fix of §2.12 item 7 took these decisions unattended, each the
recommended answer and each taken while this question stands;
`docs/DESIGN.md` §11 and §18 state the rules. Do you confirm them?

1. **A search and the self-check read the page in four orders**: pdfkit's
   reading order, the drawing order, and the page's rows (lines whose
   baselines agree, across a column gap) read from either end. In the other
   orders a word gap is read from positions (a space glyph, or a start more
   than 0.15 of the size past the glyphs before), and a space of the
   searched string matches a gap or none; inside a word no gap is allowed.
   A match found only there removes its glyphs as one occurrence per
   stretch of the glyph list, the first drawing the placeholder.
   - **(a) Keep the four orders — recommended.** No cost. Over the 960-file
     dataset (run `ps-gate5` against `ps-base2`, the main binary) the
     originals PDFium finds in a metadata rewrite's output go from 62 hits
     in 11 files to 0, each original removed (no refusal); the text
     redaction job keeps 0 PDFium hits and 0 byte hits, 909 of 909
     self-checks pass, pixel stability 99.54% and placeholders found 99.44%
     (main: 99.53% and 99.44%), both runs identical. The seeded single words
     gain a few occurrences (64 more of 9,615 in an earlier run, in 10
     files), and the dataset verb takes as long as before.
   - (b) The drawing order alone (what PDFium and pdf.js read). No
     development cost; a reader that keeps rows (pdftotext -layout) or reads
     a right-to-left line visually is not covered.
   - (c) Every pair of words adjacent in any of the orders, the space
     required. No development cost; a phrase whose words a reader joins
     without a gap (the CJK widths of dccc-0139) is missed.
2. **No word-by-word fallback**: a phrase whose words stand near each other
   in no reader's order is not a match. Measured on the
   same run: with the orders above PDFium finds no original, and a
   fallback (every word of an original within twice its word count of
   tokens) would still fire in 42 of the 812 rewritten outputs, on 317
   pages: "Free Culture" on 170 pages of a book about free culture, "2013
   Publication 15" on 59, "bed side rails", a citation reading
   "Transportation Statistics, Annual / Statistics", and the URL
   "martin-thoma.com" for "Martin Thoma" (a hyphen is no white space under
   the folding; a caller who wants such variants searches them).
   - **(a) Keep phrases as phrases — recommended.** No cost.
   - (b) Add the fallback (every word of the phrase on the page within twice
     its word count of tokens). About 2 hours and $30; the pages above lose
     their common words, or refuse.
3. **An Arabic presentation form reads as its letters under every folding**
   (the decompositions UnicodeData.txt tags `<initial>`, `<medial>`,
   `<final>`, `<isolated>`), because PDFium writes the letters (pdfbox-0004:
   "Hello محمد World" drawn in presentation forms). It is a narrower
   compatibility folding than §2.11 item 1 declines: a full-width letter and
   `ß` still stay what they are.
   - **(a) Keep it — recommended.** No cost.
   - (b) Only under a `compatibility` flag of §2.11 item 1(a). About 2 to 3
     hours and $40 to $60 with that flag; without the flag pdfbox-0004 is a
     known gap again.

### 2.15 No v0.1.0 tag: two gates of the final dataset stand failed (raised by phase 10)

`build-10.md` §4 tags `v0.1.0` only when every phase from 3 to 10 merged
`done` and every gate holds on the final dataset. On the final dataset of 960
files (runs `p10-final` and `p10-final-robust`, the release build of the
merged tip; `build-10.md` §9 holds the whole table) every leak measure is zero
and phases 3, 4, 7, 8 and 9 hold, but three items do not, so **no tag is
made**:

- **Phase 5, the reading gate**: 56 of 898 files below 0.90 F1 with PDFium,
  6.24 % against at most 5 % (median 1.0; by cause 21 pdfkit, 27 PDFium, 8
  legitimate), the numbers of §2.10 unchanged.
- **Phase 6, bitonal agreement**: 495 of 498 images equal to PDFium's bitmap,
  against 100 %. All three are PDFium's: qpdf-0026 (its first Group 4 coding
  line, `build-6.md` §9 item 4) and two of the image phase's synthesised
  files with `/Decode [1 0]` (synth-0263, synth-0265), which PDFium's
  renderer inverts as pdfkit does while `FPDFImageObj_GetBitmap` hands back
  the samples uninverted.
- **Phase 10's coverage**: 153 of 155 tags at their minimum; `gen_word_pdfa`
  and `gen_edge` wait on §2.2.

The release would ship with these known gaps; nothing else is open. Do you
want:

- **(b) Release as `0.1.0-preview`, the gaps listed in the release notes and
  the manifest — recommended**: the leak measures are zero, the gaps are a
  reader's agreement share (most of it PDFium's or decisions already on this
  list, §2.5), a PDFium bitmap convention and two generator files the laptop
  cannot make. About 1 hour and $15 (the tag, the notes).
- (a) A fix session on the causes, then `v0.1.0`: the 21 pdfkit-caused
  reading files (§2.10 (a), ~8-12 h, ~$150-250) and amending phase 6's
  bitonal gate to name PDFium differences as phase 5's does (~0.5 h, $10);
  `gen_word_pdfa` and `gen_edge` still need §2.2 answered.
- (c) No release until every gate holds as written. No cost now; phase 5's
  gate cannot hold while 27 PDFium-caused files count against it.

### 2.16 The compile cache is ccache, not sccache (raised by the build-speed chore)

The owner preferred sccache (Apache-2.0) for the shared compile cache, with
ccache (GPL-3) as the fallback if sccache could not work with this toolchain.
Measured on the laptop with sccache v0.18.0 (pinned by SHA-256 from its GitHub
release) and the ccache 4.13.6 that ships inside the pinned WinLibs toolchain:

- sccache compiles and caches GCC and clang objects (one TU of
  `src/syntax/`: a hit in 0.58 s, a miss 3.8 s slower than the compiler).
- But every sccache setting (the cache folder, the base directories that let
  two worktrees share an object) is read only by its server process, from the
  environment the server was started with; the compile client ignores its own
  (`SCCACHE_BASEDIRS` on the client: 0 hits across two trees; on the server:
  1 hit). Ninja passes no per-rule environment, so a cache in the main
  checkout shared by every worktree needs either a wrapper process per
  compile (one more process launch, about 0.25 s idle and 1 s under load,
  on every TU) or a server restarted with every worktree's path listed,
  which breaks the builds of the worktrees already using it.
- ccache takes its settings as `KEY=VALUE` arguments on its own command line,
  so CMake's compiler launcher carries them with no wrapper and no server; a
  hit costs one process (0.55 s), a cross-tree hit too (sccache's cross-tree
  hit re-ran the preprocessor: 1.97 s), and it needs no download.

So the build uses ccache (`build/compiler-cache.cmake`), as a developer tool
that is never linked into or shipped with pdfkit, like GCC itself (GPL-3 with
the runtime exception). `CLAUDE.md` §6's licence rule governs what the
library and its artefacts carry; it says nothing of build tools. Do you want:

- **(a) Keep ccache — recommended.** No cost; it is already part of the pinned
  toolchain, and a build without it (`-DPDFKIT_COMPILER_CACHE=OFF`, or ccache
  absent) compiles the same objects: the release DLL is byte-identical with
  and without it.
- (b) Switch to sccache with a per-compile environment wrapper: about 1 hour
  and $15 to $25, a fetch tool pinned by hash, and roughly 7 to 50 s more per
  clean build (one more process per TU), for a cache that sccache's own
  documentation does not promise to share between server processes.
- (c) No compile cache: no cost; every worktree rebuilds every preset from
  scratch (the release preset alone 7 to 9 minutes).
