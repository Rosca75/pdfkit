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
