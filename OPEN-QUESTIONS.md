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
