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

### 2.2 `pdfkit_version` in the charter's lifecycle sentence (raised by phase 1)

`build-1.md` adds `pdfkit_version` (the library version, the ABI version, the
build and the catalogue with each function's implementation status), but
`CLAUDE.md` §2 lists the lifecycle symbols as `pdfkit_abi_version`,
`pdfkit_close`, `pdfkit_edit_new`, `pdfkit_edit_free` and `pdfkit_buffer_free`
only. `build/check_catalogue.py` names `pdfkit_version` as its one extra
symbol until the charter says otherwise.

- **(a) Add `pdfkit_version` to the lifecycle sentence of `CLAUDE.md` §2 —
  recommended.** A one-line charter change plus removing the extra-symbol entry
  in `build/check_catalogue.py`: about 5 minutes and under $1, at the next
  phase.
- **(b) Remove `pdfkit_version` from the ABI.** About 1 hour and $10 to $15: the
  CLI would lose the catalogue it reads to report a missing function before
  opening a document, and callers would lose the build's catalogue.

### 2.3 Two sentences of `THIRD-PARTY-NOTICES.md` (raised by phase 1)

The notices say "Every component listed here has a row in
`third_party/inventory.tsv`" and that `build/check_third_party.py` "checks
that this file and the inventory name the same components". Seven of the eight
listed components are not vendored yet, so neither holds today; the check
holds the one direction that can: every inventory component is named in the
notices.

- **(a) Reword both sentences to that direction — recommended.** "Every
  vendored component has a row in `third_party/inventory.tsv` and a section
  here; `build/check_third_party.py` checks that every inventory component is
  named in this file." About 5 minutes and under $1.
- **(b) Keep the sentences and make the check two-way once phase 10 has
  vendored everything.** About 15 minutes and $2 to $3 at phase 10; until then
  the sentences stay untrue.
