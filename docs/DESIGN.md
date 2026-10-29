# DESIGN.md

This file owns the format engineering rules each layer of pdfkit implements:
how the file is read, how codes become text, how text is laid out, how images
are decoded, and how a redacted file is written and proven. `CLAUDE.md` wins on
every cross-cutting rule (the catalogue, the layers, the vocabulary, the
dependencies, the dataset, the testing tiers, the coding rules) and those rules
are cited here, never restated. Every external fact a chapter relies on has a
row in `docs/SOURCES.md` under that chapter's number, with its URL and whether
it was read there or recalled. A rule copied from a reader rather than from the
specification names the reader, because that is the fact's status. A rule marked
**author decision** is a default the owner has not ruled on and may flip.

## 1. Reading the file structure

The parser treats the cross-reference table as a hint and the bytes as the
truth, because every robust reader (PDFium, pdf.js, MuPDF, QPDF, PdfPig) does,
and producers routinely write wrong offsets, lengths and trailers.

### 1.1 The lexer

A byte walk over a span, producing tokens with their byte positions, because
reconstruction and the writer read positions and a token that can be a slice is
never copied.

- **White space** is the six bytes NUL, HT, LF, FF, CR and space; CR LF is one
  end of line. A comment runs from `%` to the end of the line and is a token
  separator.
- **Delimiters**: `(`, `)`, `<`, `>`, `[`, `]`, `{`, `}`, `/`, `%`; `<<` and
  `>>` are dictionary brackets.
- **Numbers**: an optional sign, digits, an optional decimal point with digits on
  either side or neither (`.5`, `5.`, `-.5`); a doubled sign (`--5`) reads as
  `-5` and a sign alone reads as 0, as PDFium and pdf.js read them; an integer
  that overflows 64 bits is read as a real. A number token keeps whether it was
  written as an integer, because object numbers, generations, `/W` widths and
  counts are integers and a real where an integer is expected is a parse fault.
- **Literal strings**: balanced parentheses nest by count; the escapes `\n`,
  `\r`, `\t`, `\b`, `\f`, `\(`, `\)`, `\\`, the octal forms `\d`, `\dd`, `\ddd`
  (overflow above `\377` keeps the low byte), a backslash before an end of line
  is a line continuation that contributes nothing, an unknown escape drops the
  backslash; an unescaped CR, LF or CR LF inside the string reads as one LF. An
  unterminated string ends at the end of the file and the object is marked
  repaired.
- **Hexadecimal strings**: white space between digits is skipped, an odd final
  digit is padded with `0`, a non-hex byte ends the string and marks the object
  repaired.
- **Names**: `/` followed by regular bytes; `#xx` with two hex digits decodes to
  the byte; a `#` not followed by two hex digits is kept literally, as pdf.js
  keeps it; the empty name `/` is a valid name.
- **Keywords**: `true`, `false`, `null`, `obj`, `endobj`, `stream`,
  `endstream`, `R`, `xref`, `trailer`, `startxref`; any other regular-byte run
  is an operator token (the content lexer reads those) or a fault (the object
  parser skips it and marks the object repaired).

### 1.2 The object model

A small closed set: null, boolean, number (integer or real with the integer
flag), string (bytes plus whether it was read literal or hex, so the writer can
re-emit the form it read), name, array, dictionary (insertion order kept and
duplicate keys resolved last-wins, because the writer reproduces order),
stream (dictionary plus the raw data's position and length in the file; decoded
bytes on demand through the filters of §3) and reference (object number and
generation). The model holds no semantics; it is the file's own vocabulary.

### 1.3 Objects and streams

- `n g R` is recognised by a two-token lookahead after an integer.
- `n g obj ... endobj` is read at an offset. A missing `endobj` ends the object
  at the next `n g obj` or at the end of the file, and the object is marked
  repaired.
- A stream's `/Length` (direct or a reference resolved without recursion into
  the stream being read) is trusted only when the bytes at the data start plus
  the length, after optional white space, read `endstream`; otherwise the data
  ends at the next `endstream` found by scanning forward, less the end of line
  before it, which is pdf.js's and QPDF's recovery, and the object is marked
  repaired. The data starts after `stream` and one CR LF or one LF (a lone CR is
  accepted, as viewers accept it).
- The parser never follows a reference; resolution belongs to the document, which
  caches resolved objects, answers a missing object as null, and refuses a
  reference cycle met while resolving a `/Length`.

### 1.4 The cross-reference table and stream

- **The header** is searched for within the first 1,024 bytes, as PDFium, QPDF
  and pdf.js search, because garbage before `%PDF-` is common; every later
  offset is taken relative to where `%PDF-` was found.
- **`startxref`** is searched for in the last 2,048 bytes. Its offset must land
  on `xref` or on `n g obj` of a cross-reference stream; otherwise the reader
  reconstructs (§1.6).
- **The classic table**: subsections of `first count`, each entry read as three
  white-space-separated fields (offset, generation, `n` or `f`) rather than as a
  fixed 20 bytes, because producers write 19- and 21-byte entries. A subsection
  whose first object is 1 and whose entry 0 is not free is read as off by one
  and corrected, as pdf.js corrects it.
- **The cross-reference stream**: `/W` with three field widths (a width of 0
  means the default: type 1 for the first field, 0 for the generation), `/Index`
  pairs defaulting to `[0 /Size]`, type 0 free, type 1 offset, type 2 object
  stream number and index. A `/W` width above 8 bytes is a fault.
- **Hybrid files**: a classic trailer's `/XRefStm` is read after the table it
  belongs to, its entries filling only numbers the table does not name.
- **The `/Prev` chain** is followed newest first with a visited set of offsets,
  so a loop ends; the first entry read for an object number wins, which is the
  specification's rule for incremental updates.

### 1.5 Object streams

An object stream's `/N` pairs of object number and offset, offsets relative to
`/First`, are parsed once and cached per stream, through the filters and the
limits of §3. An object stream never holds a stream or a generation other than
0; one that claims to is a fault for that entry only.

### 1.6 Reconstruction

Reconstruction runs whenever the first path disappoints, and the reason is
recorded on the document (the `repaired` warning, §19), because a caller deciding
whether to trust an extraction needs to know it came from a scan. The triggers:
`startxref` missing or not landing on a table or stream; an offset that does not
land on `n g obj` of the expected number; a missing trailer or `/Root`; an
object that fails to parse where the table placed it; a `/Prev` that points
nowhere; a linearised file whose first-page object count disagrees with its
hint.

The scan is one forward pass over the whole file for `n g obj` at a token
boundary, `endobj`, `trailer`, `startxref` and `%%EOF`:

- the **last** occurrence of an object number wins, because an incremental
  update appends the newer version (pdf.js's index scan, PDFium's rebuild,
  MuPDF's repair);
- every object stream found is opened and indexed; an object inside a stream
  yields to an object of the same number outside one only when the outside one
  is later in the file and parses;
- the trailer is rebuilt from every `trailer` dictionary and every
  cross-reference stream dictionary found, later over earlier; where `/Root` is
  missing or wrong the object with `/Type /Catalog` (the last one) is taken,
  where `/Info` is missing the object with `/Producer` or `/Title`, where
  `/Encrypt` is missing but encrypted strings are evident the object with
  `/Filter /Standard`, which is MuPDF's repair;
- a file in which the scan finds no object with `/Type /Page` is refused
  (`no_page`, §19).

A file with no `%PDF-` in the first 1,024 bytes is scanned all the same; it is
refused as `not_pdf` only when the scan finds no page either.

## 2. Encryption

The standard security handler is implemented for reading only, with the empty
user password only, because almost every "protected" business PDF protects
against editing and not against reading, and trying passwords is out of scope.

- **Revisions 2, 3 and 4**: the padding string of ISO 32000-1 §7.6.3.3 as a
  constant; algorithm 2 (the file key from the padded empty password, `/O`,
  `/P` as four little-endian bytes, the first `/ID` element, `0xFFFFFFFF`
  appended when revision 4 has `/EncryptMetadata false`, fifty MD5 iterations
  from revision 3, key length `/Length / 8` defaulting to 5 bytes); algorithm 4
  (revision 2) or 5 (revisions 3 and 4) to validate the user password against
  `/U` (the first 16 bytes from revision 3).
- **Per-object keys**: the file key plus the low three bytes of the object number
  and the low two of the generation, plus `sAlT` for AES, MD5-hashed and cut to
  `min(n + 5, 16)` bytes (algorithm 1).
- **RC4** (40-bit and 128-bit) is written in the platform layer, about twenty
  lines; **AESV2** is AES-128 in CBC with the IV as the first 16 bytes of the
  data and PKCS#7 padding removed (a bad pad is tolerated and the bytes kept).
- **Revisions 5 and 6 (AESV3)**: the user password validated against the first
  32 bytes of `/U` with the validation salt (algorithm 2.A); revision 5 hashes
  with plain SHA-256; revision 6 uses the hash loop of ISO 32000-2 algorithm 2.B
  (rounds of AES-128-CBC over 64 repetitions of password, key and user-key
  material, the next hash chosen among SHA-256, SHA-384 and SHA-512 by the sum
  of the first 16 encrypted bytes modulo 3, at least 64 rounds and until the last
  byte is not greater than the round number minus 32); the file key is
  `/UE` decrypted with AES-256 in CBC with a zero IV and no padding under the key
  hashed with the key salt. `/Perms` is checked: one that does not decrypt to
  "adb" means the file key is wrong and is refused `password_required`, as PDFium
  refuses it, while a missing `/Perms` or one naming other permissions than `/P`
  is a repair note. SASLprep reduces to nothing for the empty password.
- **Crypt filters**: `/CF` with `/StdCF` (`/CFM` `/V2` RC4, `/AESV2`, `/AESV3`),
  `/StmF` and `/StrF` choosing them, `/Identity` meaning no decryption, a stream
  whose own `/Filter` names `/Crypt` with `/Name` using that filter.
- **What is not decrypted**: the cross-reference stream, the strings inside an
  object stream (the stream as a whole is decrypted), the `/Encrypt` dictionary's
  own strings, the `/ID` array, and the XMP metadata stream when
  `/EncryptMetadata` is false.
- **Refusals**: a non-empty user password, or a revision 5 or 6 `/Perms` that
  does not decrypt, is `password_required`; a handler other than `/Standard` (a
  public-key or vendor handler), or a revision, version, crypt filter or method
  the standard handler does not define, is `unsupported_security_handler`
  (§19).
- **Output**: every file pdfkit writes is unencrypted, because a redacted copy is
  a new document and the original's restrictions were set for the original.

## 3. Filters and limits

### 3.1 The non-image filters

Each is a short routine in the syntax layer, applied in the order `/Filter`
lists them with the matching `/DecodeParms` entry:

- **FlateDecode** through zlib's inflate with a raw-or-zlib header tolerance;
  a damaged stream yields what inflated before the damage, the object is marked
  damaged and a `damaged_stream` warning names the page, because every viewer
  reads it so and a page half read is still a page the caller can decide about.
- **LZWDecode** with `/EarlyChange` (default 1), code widths 9 to 12, the clear
  and end-of-data codes.
- **ASCII85Decode**: `z` for four zero bytes (outside a group only), white space
  skipped, `~>` ends, a final partial group padded with `u`; **ASCIIHexDecode**:
  white space skipped, `>` ends, an odd final digit padded with `0`.
- **RunLengthDecode**: lengths 0 to 127 copy, 129 to 255 repeat, 128 ends.
- **Predictors** after Flate and LZW: `/Predictor` 2 (TIFF, per component, with
  `/BitsPerComponent` 1, 2, 4, 8 or 16) and 10 to 15 (PNG, the filter type read
  per row whatever the predictor number says, as every reader does), with
  `/Colors`, `/BitsPerComponent` and `/Columns`.
- **Crypt** is the identity after §2 has decrypted the stream.
- **The image codecs** (`DCTDecode`, `CCITTFaxDecode`, `JBIG2Decode`,
  `JPXDecode`) are not parser filters: a filter chain stops at the first of them
  and hands back the encoded bytes and the remaining filter names, because the
  images layer decodes them (§9) and the writer copies them untouched.
- Abbreviated names in inline images (`/Fl`, `/LZW`, `/A85`, `/AHx`, `/RL`,
  `/DCT`, `/CCF`) are mapped to the full names.

### 3.2 The limits

Every decoded byte is counted against two limits: 512 MiB per stream and 2 GiB
per document, both configurable at open, counted in bytes produced, because a
stream length is a header's claim and a few kilobytes of Flate can inflate to
gigabytes. Past a limit, open (or the call that decoded the stream) is refused
with `limit_exceeded`, naming the object, the limit and the way out (re-save or
split the file, or raise the limit at open).

The same budget bounds every allocation sized from the file (CLAUDE.md §9):
the object count (author decision: a ceiling of 8,388,607 objects, the largest
number a classic table can address in practice), the image pixel count
(width times height times components, checked before decoding against the
per-stream limit), the INDEX and table counts of font programs, and the content
operator count per page (author decision: 50 million).

The **page ceiling** defaults to 10,000 pages (author decision) and is
configurable at open; past it, open is refused with `page_ceiling`, naming the
count and the ceiling.

## 4. Pages, resources and content streams

- **The page tree** is walked depth first from the catalog's `/Pages`; a `/Kids`
  entry already on the current path is a cycle and is skipped with a note; a kid
  that is neither `/Pages` nor `/Page` is read by its `/Kids` or `/Contents`
  (a node with `/Kids` is a node, one with `/Contents` or no `/Kids` a page), as
  pdf.js reads it. Pages are numbered from 1 in that order.
- **Inherited attributes**: `/Resources`, `/MediaBox`, `/CropBox` and `/Rotate`
  are carried down and recorded on each page; a missing `/MediaBox` is US Letter
  (0 0 612 792), as viewers default it; `/CropBox` is clipped to `/MediaBox`;
  `/Rotate` is normalised to 0, 90, 180 or 270.
- **Resources**: a name is looked up in the current content's `/Resources`;
  a form XObject with no `/Resources` falls back to its parent's (the page's or
  the drawing form's), which the specification deprecates and every viewer
  tolerates; a name that resolves to nothing is a `resource_missing` warning and
  the operator draws nothing.
- **The content lexer** reads operands (the object syntax without references)
  and operators as tokens with byte positions, over the concatenation of a page's
  `/Contents` array with one newline between pieces, because the specification
  says the pieces split only at token boundaries "unrelated to the page's logical
  content" and an operator may span two pieces. Each operator is reported with
  its operand list and its byte range, and the range maps back to the piece and
  the offset inside it.
- **Inline images**: the dictionary from `BI` to `ID`, then one white-space byte,
  then the data. When the dictionary makes the uncompressed length computable
  (no filter, or only ASCII filters with a computable end, width, height, bits
  per component and components known), that length is used, because `EI` can
  occur inside binary data; otherwise the data ends at the first `EI` preceded by
  white space and followed by white space or the end of the stream, whose
  following bytes lex as operators (PdfPig's check against a false `EI`).
- **What is never followed**: an external stream (`/F` on a stream), a `/URI`,
  a `/Launch`, `/GoToR` or `/GoToE` action, JavaScript, an embedded file's
  content, and any resource not inside the bytes given, because the library
  names no path and opens no connection (CLAUDE.md §1, §9).

## 5. Fonts and the decoding ladder

### 5.1 Font kinds

A font dictionary dispatches on `/Subtype`: `Type1`, `MMType1` and a missing or
unknown subtype are read as Type 1 (as viewers read them); `TrueType`; `Type3`;
`Type0` with a descendant `CIDFontType0` or `CIDFontType2`. A font with no
`/BaseFont` is named `unnamed`. Fonts are cached per object number within a
document. A font that fails to load in any part yields a font whose codes decode
through the rungs that remain or as unreadable, and never an exception out of
the loader, because one bad font must not lose a page.

### 5.2 Simple-font encodings

- The base encoding is `/Encoding` as a name (`StandardEncoding`,
  `WinAnsiEncoding`, `MacRomanEncoding`, `MacExpertEncoding`), or the
  dictionary's `/BaseEncoding`, or the font program's built-in encoding for an
  embedded symbolic font, or Standard for a non-symbolic font. The tables are
  those of ISO 32000-1 Annex D, with WinAnsi's unassigned codes (other than
  space and the control range) mapped to `bullet`, as Acrobat maps them, and the
  built-in encodings of Symbol and ZapfDingbats.
- `/Differences` replaces entries of the base encoding code by code.
- A glyph name becomes text through the Adobe Glyph List; failing that through
  the naming conventions pdf.js reads: `uniXXXX` (and sequences of four-digit
  groups), `uXXXX` to `uXXXXXX`, `gNN`, `glyphNN`, `cidNN`, `GNN`, `Cdd`
  (decimal) and `Cxx` (hexadecimal, tried after decimal); a suffix after a
  period is dropped (`a.sc` reads as `a`); a name `xx_yy` reads as the ligature
  of its parts. The `gNN`, `Cdd` and `cidNN` forms name glyph or code numbers,
  not characters, so they decode only when the font program maps that glyph
  through its own `cmap` or charset; otherwise they are unreadable.
- **Symbolic TrueType** (flag 3 of `/Flags` set, or no usable encoding): the
  font program's `cmap` is tried in the order (3,0) with the code, then with the
  code plus `0xF000`, then (1,0) with the code, then (3,1) through the glyph name
  of the encoding, which is pdf.js's and PDFBox's order, and last (3,1) with the
  code itself, as pdf.js reads a symbolic font whose only cmap is Unicode; ISO
  32000-1 §9.6.6.4 says these heuristics are "not described here", so this rung
  is the one the agreement gate watches most closely. The glyph found reads as
  text through the program's Unicode cmaps, then its `post` name.
- A font resource that names no font dictionary (a missing object, an object
  in an undecodable object stream) decodes every code as unreadable: no
  encoding is known to guess from.

### 5.3 Composite fonts and CMaps

- `/Encoding` is `Identity-H` or `Identity-V` (two-byte codes, CID equals code),
  or an embedded CMap stream, or a predefined CMap name.
- The CMap reader handles `begincodespacerange` (the byte length of each code
  is decided by the codespace ranges, shortest match first, as the
  specification's algorithm does), `begincidrange`, `begincidchar`,
  `beginbfrange` (destination as one string incremented in its last byte, or as
  an array of strings), `beginbfchar`, `beginnotdefrange`, and `usecmap` for an
  embedded CMap only, resolved against the identities. Destinations are UTF-16BE
  with surrogate pairs kept whole; a malformed entry is skipped and counted,
  because one bad line is not a reason to lose the map.
- A predefined CMap other than the identities ships no data, so a code of such a
  font with no `/ToUnicode` is unreadable and counted (no CJK CMap data is
  shipped). Its codes are split by the codespace its name implies (the
  Shift-JIS, EUC, GBK, Big5 and UHC lead bytes, else two bytes), so they are
  counted as the encoding writes them. The Unicode CMaps (`Uni...-UCS2-`,
  `-UTF16-`, `-UTF8-`, `-UTF32-`) are the exception: their codes are the
  characters themselves and decode without data.
- A `/ToUnicode` written as the name `Identity-H` or `Identity-V` reads each
  two-byte code as its own UTF-16 value, as pdf.js reads it; a `bfrange` whose
  bounds differ in more than their last byte is read as the numeric range, as
  pdf.js reads it, because whole identity ranges are common.
- `/CIDToGIDMap` is `Identity` or a stream of two-byte glyph ids; a CFF-keyed
  descendant maps CID to glyph through its charset.

### 5.4 The decoding ladder

Each code is decoded by the first rung that answers, and the rung is recorded on
every glyph record (CLAUDE.md §4):

1. `to_unicode`: the font's `/ToUnicode` CMap (`bfchar`, `bfrange` in both forms,
   multi-byte codes, surrogate pairs). A destination that is empty, a control
   character other than tab, or a private-use code point falls through to the
   next rung, because a ToUnicode entry that maps to nothing usable is a
   producer's placeholder, not an answer.
2. `glyph_name`: for a simple font, the glyph name through the encoding and the
   glyph list (§5.2); for a CID font, the glyph name from a CFF charset or a
   TrueType `post` table.
3. `font_program`: the font program's own map (a TrueType `cmap` per §5.2, a Type 1
   program's built-in `/Encoding`, a CFF's encoding and charset).
4. `actual_text`: a marked-content `/ActualText` span covering the glyph replaces
   the span's decoded text when the two differ, and every glyph of the span
   records this rung, because LibreOffice and Chrome write it exactly where
   `/ToUnicode` cannot express a ligature or cluster; the interpreter (§6)
   applies it.
5. `unreadable`: the code is recorded with no text and counted per page and per
   font for the `unreadable_text` warning; it is never emitted as a private-use
   character, as U+FFFD or as `(cid:N)`, because a string the caller cannot act
   on reads as text that is not there.

A Type 3 font is reached through rungs 1 and 2 only. Text drawn as vector
outlines is not text; it is a figure.

### 5.5 Ligatures

The decoded text of a code is decomposed through a fixed table (`ﬀ` U+FB00 to
`ﬆ` U+FB06: ff, fi, fl, ffi, ffl, ſt, st; and the glyph names `ff`, `fi`, `fl`,
`ffi`, `ffl`, `st`), and the decomposition is recorded on the record, so one code
still points at the two or three characters it drew. Combining sequences are kept
as written; folding is the matcher's (§11).

### 5.6 Widths

The advance of a code comes from, in order: `/Widths` with `/FirstChar` (a code
outside the array takes `/MissingWidth` from the descriptor, default 0), or `/W`
in both array forms with `/DW` (default 1000) for a CID font, which therefore
never reaches the program; then, for a simple font without `/Widths`, the font
program (a TrueType `hmtx` scaled by `head.unitsPerEm`, a CFF charstring's width
from the first stack-clearing operator with `nominalWidthX` and `defaultWidthX`,
a Type 1 charstring's `hsbw` or `sbw` after eexec and charstring decryption);
then the Core 14 metrics for a non-embedded standard font. The Core 14 table is
reduced from Adobe's AFM files to font, glyph name and width; the alias table
maps `Arial`, `ArialMT` to Helvetica, `Arial,Bold` and `Arial-BoldMT` to
Helvetica-Bold, `TimesNewRoman` and `TimesNewRomanPSMT` to Times-Roman,
`CourierNew` to Courier, the `,Bold`, `,Italic`, `,BoldItalic` and `-Bold`
suffix forms to the matching face, after the subset prefix `ABCDEF+` is
stripped. A Type 3 width is in glyph space and scaled through `/FontMatrix`.
The width is what places every later glyph and what the surgery of §12 keeps, so
a wrong width is a defect in both extraction and redaction.

### 5.7 The font-program readers

Each reads only what encodings and widths need, because nothing in pdfkit draws:
TrueType and OpenType (the table directory; `cmap` formats 0, 4, 6 and 12;
`hhea`, `hmtx`, `head`, `maxp`; `post` format 2 names; `loca` and `glyf` only to
say whether a glyph is empty; `CFF ` handed to the CFF reader); CFF (header,
INDEX, top dictionary, strings, charsets 0, 1, 2 and the predefined ISOAdobe,
Expert and ExpertSubset, encodings standard, expert or custom with supplements,
private dictionary widths, CID-keyed `FDArray` and `FDSelect`); Type 1 (PFB
segment headers skipped, the clear-text `/Encoding`, eexec with r = 55665 and
four lead bytes, `lenIV`, charstrings decrypted with r = 4330 as far as `hsbw` or
`sbw`). A truncated or malformed program answers what it read before the fault.

## 6. The content interpreter and the glyph record

One interpreter serves extraction and redaction (CLAUDE.md §3.2). It runs over a
page's concatenated content and, recursively, every form XObject it draws.

### 6.1 The state machine

- Graphics state: `q` and `Q` (a stack with a depth limit of 256: a `q` past
  it is counted and saves nothing and the `Q` that closes it restores nothing,
  an author decision; an unmatched `Q` is ignored, an unclosed `q` is closed at
  the end), `cm`, `w`, `J`, `j`,
  `M`, `d`, `ri`, `i`, `gs` (with `/Font`, `/SMask` and `/CA`/`/ca` noted).
- Colour: `CS`, `cs`, `SC`, `SCN`, `sc`, `scn`, `G`, `g`, `RG`, `rg`, `K`, `k`,
  recorded as the colour-space name and its components.
- Text object: `BT` resets the text and line matrices; `ET`; text state `Tc`,
  `Tw`, `Tz`, `TL`, `Tf`, `Tr`, `Ts`; positioning `Td`, `TD` (sets `TL` to the
  negated ty), `Tm`, `T*`.
- Showing: `Tj`, `TJ`, `'` (`T*` then `Tj`), `"` (sets `Tw` and `Tc`, then `'`).
  Each code advances the text matrix by
  `tx = ((w0 - adj/1000) * Tfs + Tc + Tw) * Th` (horizontal writing) where `w0`
  is the width in thousandths, `adj` the preceding `TJ` number (zero otherwise),
  `Th = Tz/100`, and `Tw` applies to the single-byte code 32 only, never to a
  multi-byte code, which is ISO 32000-1 §9.4.4. A `TJ` number moves the position
  by `-adj/1000 * Tfs * Th` without a glyph. Vertical writing (`Identity-V`) uses
  the vertical advance (`/W2`, `/DW2`, default `[880 -1000]`) on ty.
- Clipping: `W` and `W*` intersect the clip with the current path's bounding
  rectangle in device space; the clip is a rectangle bound, because a glyph's
  visibility needs no more precision than that.
- Paths: `m`, `l`, `c`, `v`, `y`, `h`, `re`, then `S`, `s`, `f`, `F`, `f*`,
  `B`, `B*`, `b`, `b*`, `n`, `sh`; the painting operators between one `q` and its
  `Q` are one figure with its device box.
- Marked content: `BMC`, `BDC` (properties inline or by name through
  `/Properties`), `EMC`, `MP`, `DP`; an `/ActualText` span and an `/OC` group
  are noted on every glyph inside them; an unbalanced `EMC` is ignored.
- XObjects: `Do` of a form walks the form's content with its `/Matrix`
  concatenated and its `/BBox` as a clip, in a saved graphics state; a form drawn
  twice is walked twice and its records carry the form path; a form already on
  the current path (it draws itself) is refused for that draw with a note, and
  the depth is limited to 32. `Do` of an image and `BI` record an image
  occurrence (§9.2). Patterns and soft masks are not walked, as MuPDF does not
  walk them; an image inside one is listed with `image_in_pattern` (§19).
- Type 3 glyph procedures are not walked for text; the Type 3 glyph is one glyph
  record, its box from `/FontBBox` and `/FontMatrix`.
- An unknown operator is skipped with its operands; a damaged stream is
  interpreted as far as it lexes and the page is noted.

### 6.2 The glyph record

Every shown code becomes one record with the fields CLAUDE.md §4 lists. The
device box is the glyph's origin and advance under `Trm = [Tfs*Th 0 0 Tfs 0 Trise]
× Tm × CTM`, with the font descriptor's ascent and descent (or `/FontBBox`,
or 0.8 and -0.2 of the size when neither is usable) as its height. The record
holds the absolute position of every glyph, because both keeping and
re-anchoring the following glyphs after a redaction read it.

### 6.3 The re-serialisation identity

Writing a page's operators back, with each text-showing operator rebuilt from its
glyph records and the bytes between codes (`TJ` numbers, white space,
delimiters) kept, reproduces the content stream's bytes exactly. This identity
is tested over every unit fixture page and every dataset page that opens,
because an off-by-one in a record's byte range is invisible to every text
assertion and fatal to the surgery of §12.

## 7. Layout

Glyph records become words, lines and blocks in reading order, per page, the
visible set first and the hidden set after (§8).

- **Words**: two consecutive glyphs on one line are one word when the gap
  between the first's advance end and the second's origin is under 0.1 of the
  font size on a line that draws no space glyph (pdfminer's `word_margin`),
  under 0.3 on a line that draws space glyphs (its other gaps are letter
  spacing and kerning), and under 1.0 between two ideographic, kana, hangul or
  full-width glyphs (scripts written without spaces); beyond 2.0 of the font
  size the line ends there, a block boundary. A decoded space or a `TJ`
  adjustment wider than the word gap both end a word, because a space glyph is
  routinely absent and kerning numbers routinely split words. The dataset
  measured the gaps: inside words they lie within 0.07 of the size, between
  positioned words from 0.15 up.
- **Lines**: glyphs whose baselines (the text line, the rise left out) agree
  within a fifth of the font size, in the direction their advance runs (a
  negative font size or scaling runs it backwards), so rotated text forms its
  own lines along its rotation; vertical text (`Identity-V` or a text matrix
  rotated by 90°) forms vertical lines read top to bottom. Along one baseline
  the content's runs (glyphs drawn one after another, each at or after the one
  before) are kept whole and ordered by where they start, so a string drawn
  over another reads as two strings rather than as their letters interleaved;
  a run of one or two glyphs takes its place by position.
- **Direction**: a line whose strong characters are mostly right-to-left
  (Hebrew, Arabic) reads from its far end, its left-to-right runs (Latin words,
  numbers) kept left to right; a left-to-right line reads its right-to-left
  runs from their far ends. This is the logical order the producer's drawing
  order gives when it drew logically, found from the positions so a producer
  drawing in visual order reads the same (author decision); the line carries
  its direction.
- **Blocks and order**: lines grouped into blocks by Docstrum (lines of one
  direction that overlap along it and lie within 1.3 times the page's
  between-line distance of each other, a ruling drawn between them keeping
  them apart), the blocks ordered by a recursive XY-cut over their boxes in the
  page's dominant text direction, columns before rows, both ported from
  PdfPig's page segmenters (Apache 2.0, the port's header names the source). A tagged PDF's structure order is used instead when `/StructTreeRoot`
  is present and every marked-content item it orders resolves to glyphs of the
  page, because it is the producer's own statement of reading order.
- **Hyphenation**: a line ending in a hyphen (U+002D or U+00AD) followed by a
  line starting with a lower-case letter is one word across the two lines, the
  hyphen attached to the first half, so a match finds the word and a redaction
  removes the hyphen; a hyphen followed by a capital is kept, because
  "Jean-"/"Pierre" is a name. The two halves stay words of their own lines and
  the first is marked `hyphenated`, so every range stays inside its line. The
  same rule joins across a page break only when both pages are extracted
  together.
- **Duplicates**: a glyph with the same code and font (by name, so one face
  stated as two font objects is one font) as an earlier glyph whose origin lies
  within a tenth of the font size of it (a fake bold or a shadow) is folded into
  the earlier record: the earlier record lists it among its duplicates, so a
  redaction removes both, and the copy is a record of its own in the hidden
  set with the class `duplicate` and its partner named, so the text is read
  once. A glyph of no size is never a duplicate.
- **Determinism**: every sort is stable and every geometric key is rounded to
  0.01 pt (CLAUDE.md §9); the PdfPig nearest-neighbour word extractor's
  documented tie-order defect is not carried over.
- **Output shape**: per page, blocks in reading order, each with lines, each
  with words; every word, line and block carries its box and the half-open range
  of glyph indices it covers in the page's glyph list, which is in reading order
  (the visible blocks' glyphs, then the hidden blocks'; each record names its
  draw order), so a match found in text maps back to records without a second
  reader.

## 8. Hidden text

Every glyph in the file is text: it is extracted, flagged with every class it
belongs to, and redactable, because an extractor's copy reaches it (CLAUDE.md
§5). The classes:

| Class | Definition |
|---|---|
| `render_invisible` | render mode 3 (invisible) or 7 (clip only) |
| `white_fill` | fill colour DeviceGray 1, DeviceRGB 1 1 1 or DeviceCMYK 0 0 0 0 (or a Separation or ICC colour whose alternate reads so) with no figure or image drawn earlier under the glyph's box |
| `clipped` | the glyph's box lies wholly outside the clip rectangle in force |
| `off_media` | the box lies wholly outside the media box |
| `off_crop` | the box lies inside the media box and wholly outside the crop box |
| `optional_content_off` | the glyph is inside an optional-content group (or membership dictionary) that the default configuration sets OFF |
| `duplicate` | the glyph was folded into an earlier record (§7) |
| `covered` | an image without a mask, or an opaque filled rectangle (one `re` under an axis-aligned CTM, so its bound is what it paints), drawn later wholly covers the glyph's box |

A page's visible blocks come first in reading order, then its hidden blocks laid
out by the same rules, and the page reports a `hidden_text` warning with the
count per class.

## 9. Images

### 9.1 Identity

An image's identity is defined in CLAUDE.md §4. An inline image is hashed the
same way over its data and its abbreviated dictionary, with abbreviated keys and
names expanded first so an inline and an XObject copy of one picture share an
identity. The identity survives renumbering, page selection and a producer that
stores one logo twice.

### 9.2 Occurrences

The interpreter numbers `Do` of an image and `BI` per page in draw order,
through forms, from 0; an occurrence is the page number, the ordinal, the device
box (the unit square under the CTM) and the form path, never a byte offset,
because a redaction changes every offset and the writer runs the same walk over
the same objects.

### 9.3 Decoding by encoding

| `/Filter` (last of the chain) | Decoded by |
|---|---|
| none, `FlateDecode`, `LZWDecode`, `RunLengthDecode`, `ASCII85Decode`, `ASCIIHexDecode`, with predictors | the syntax filters (§3), then the colour pipeline (§9.4) |
| `DCTDecode` | libjpeg-turbo; CMYK and YCCK JPEGs decoded to CMYK as stored, an Adobe APP14 marker inverting nothing, as PDFium reads them (measured: inverting the dataset's YCCK JPEGs makes them differ from PDFium by a mean of 248 per channel); a `/Decode` array inverts where a file means it |
| `JPXDecode` | OpenJPEG; the codestream's own colour space wins over `/ColorSpace` when the dictionary has none; `/SMaskInData` 1 or 2 takes the alpha channel as the soft mask |
| `JBIG2Decode` | written here, ported from pdf.js's `jbig2.js` (Apache 2.0): the file header and embedded organisation, `/JBIG2Globals`, generic regions (arithmetic with templates 0 to 3, TPGDON, and MMR), refinement regions, symbol dictionaries and text regions (with and without Huffman tables, the standard tables), pattern dictionaries and halftone regions, page information and end-of-stripe; 1 is black unless `/Decode [1 0]` |
| `CCITTFaxDecode` | written here, ported from pdf.js's `ccitt.js` (Apache 2.0): `/K` < 0 (G4), 0 (G3 1D), > 0 (G3 2D), `/Columns`, `/Rows`, `/EncodedByteAlign`, `/EndOfLine`, `/EndOfBlock`, `/BlackIs1`, `/DamagedRowsBeforeError` |

A stream that fails to decode yields an `undecodable_image` warning naming the
identity, the encoding and the fault; it is never omitted in silence, and its
treatments are limited to keep and remove (§14).

### 9.4 Colour and samples

- `/BitsPerComponent` 1, 2, 4, 8 and 16; rows padded to a byte boundary; `/Decode`
  arrays applied per component (the default per colour space, `[1 0]` inverting);
  `/ImageMask true` is a 1-bit stencil painted in the fill colour.
- Colour spaces: DeviceGray, DeviceRGB, DeviceCMYK; `Indexed` (base, hival,
  lookup string or stream); `ICCBased` read by its `/N` component count with its
  `/Alternate` and no colour management; CalGray and CalRGB as their device
  equivalents; `Lab` converted with its white point; `Separation` and `DeviceN`
  through their alternate space and tint transform. Tint transforms implement
  function types 0 (sampled), 2 (exponential), 3 (stitching) and 4 (the
  PostScript calculator, with an operator budget per evaluation and a
  per-image cache of evaluated inputs). CMYK becomes RGB through PDFium's
  Adobe CMYK table (a 9 x 9 x 9 x 9 grid of sRGB samples interpolated per
  axis, ported from PDFium under BSD-3), because the naive complement
  (`R = (1-C)(1-K)`) differs from the independent reader by 10 to 20 per
  channel on real CMYK images; no colour management is vendored, so an
  `ICCBased` profile is read as its device space (both decisions wait on the
  owner in `OPEN-QUESTIONS.md`).
- Masks: `/SMask` (a grey image, with `/Matte` un-premultiplied), `/Mask` as a
  colour-key range array or as a stencil image; a mask whose size differs from
  the base is kept at its own size.
- Every decode checks width × height × components × bits against the budget
  before allocating (§3.2).

### 9.5 Extraction output

`pdfkit_image_data` answers, per identity: for a `DCTDecode` source, the
original JPEG bytes unchanged, because re-encoding a JPEG loses quality and adds
nothing; for every other source, and for a DCT source asked with `decode_jpeg`,
a PNG (1-bit greyscale for a bitonal image, greyscale or RGB at 8 bits, 16 when
the source is 16-bit, with an alpha channel when the image carries one) written
by the library's own PNG writer over zlib; the mask
(soft or stencil) as a separate PNG when asked for, because combining masks of a
different size is a resampling decision the caller should make. An image inside
a pattern or a soft mask is listed with `image_in_pattern` and its occurrences
are not numbered.

## 10. Metadata

- **Fields**: from the Info dictionary `Title`, `Author`, `Subject`, `Keywords`,
  `Creator`, `Producer`, `CreationDate`, `ModDate`; from the XMP packet
  `dc:title`, `dc:creator`, `dc:description`, `dc:subject`, `xmp:CreatorTool`,
  `pdf:Producer`, `pdf:Keywords`, `xmp:CreateDate`, `xmp:ModifyDate`. They pair
  as Title–dc:title, Author–dc:creator, Subject–dc:description,
  Keywords–pdf:Keywords (and dc:subject), Creator–xmp:CreatorTool,
  Producer–pdf:Producer, CreationDate–xmp:CreateDate, ModDate–xmp:ModifyDate.
- **Reading**: an Info string is UTF-16BE when it starts with the BOM `FE FF`,
  UTF-8 when it starts with `EF BB BF` (PDF 2.0), UTF-16LE (tolerated and
  counted) after `FF FE`, PDFDocEncoding otherwise; dates are kept as written
  and also normalised to ISO 8601 when they parse. The XMP packet is the
  catalog's `/Metadata` stream, scanned as XML with every prefix resolved
  through the declarations in scope (so the old `xap:` prefix reads as `xmp:`)
  for the properties of every `rdf:Description`, in element or attribute form
  (`rdf:Alt` taking `x-default` first, `rdf:Seq` and `rdf:Bag` items joined
  with `; `). A packet in UTF-16, one whose tags do not close or match, one
  with an unterminated quote, comment or section, or one with no `rdf:RDF` is
  unreadable, with the reason named; an empty packet states nothing.
- **Side by side**: one row per pair, with both values and whether they agree,
  because viewers show whichever they prefer and a file whose two statements
  disagree says two things.
- **Writing**: a field the caller sets is written to both places; a field set
  to `null` is removed from both; a field not set keeps its value in both.
  Info strings are written as UTF-16BE with BOM when not representable in
  PDFDocEncoding; a date is written as a PDF date in the Info dictionary and
  in ISO 8601 in the packet when it parses as either, and as given otherwise.
  An Info dictionary is created when a field is set and the file has none; a
  packet is not. The XMP packet is edited in place: the content of every
  property the field pairs with is replaced (XML-escaped, its start tag and so
  any namespace it declares kept) and every other byte kept, so custom
  namespaces survive; a property the packet lacks is added in an
  `rdf:Description` of its own before the `rdf:RDF` end tag; a packet that
  will not scan is dropped with `xmp_unreadable` rather than guessed at, and
  one that states nothing is dropped in silence. **A value outside the field
  list that repeats a redacted string goes** (author decision, open as
  `OPEN-QUESTIONS.md` §2.12): a custom Info entry, an XMP property no field
  pairs with (`xmp:MetadataDate`, `dc:rights`, a custom namespace's) and the
  packet's toolkit attribute `x:xmptk` are removed when one of the redacted
  strings (the original values of the rewritten fields, the strings the text
  redaction removes) occurs in them with every folding on, because no
  function can rewrite them and the self-check would refuse the output. On
  every write, whatever the plan, the packet loses every property of the
  media-management namespace, whatever its prefix, but `xmpMM:DocumentID` and
  `xmpMM:InstanceID`, whose values are regenerated, as is the trailer `/ID`,
  from the SHA-256 of the output's content (§17: written as placeholders and
  patched, `xmp.did:` and `xmp.iid:` plus the first and second 16 bytes of the
  hash in hexadecimal), because the original's identifiers identify the
  original; a packet without identifiers gains none.

## 11. Matching

A match (CLAUDE.md §4) arrives in one of three forms, each with its
placeholder:

- **Glyph ranges** from an earlier extraction: page and half-open glyph-index
  range, with the document fingerprint (the SHA-256 of the file from its
  `%PDF-` header on) the extraction reported; a range from another document or
  out of range is refused, because a stale range redacts the wrong glyphs. A
  glyph range is an expression at a location, not a value: it removes its
  glyphs and is verified there (§18), never searched elsewhere. It may be an
  array of ranges on one page, read as one expression (the form an
  occurrence found in another order takes, which covers several stretches of
  the glyph list): the ranges are sorted, none may be empty or overlap
  another, the placeholder is drawn once at the first and the others are
  holes, and the answer is one occurrence whose text is the ranges' text
  joined by a space. One place includes every copy drawn there: with its
  glyphs go those of every occurrence of its text, in any order of this
  section, that has a glyph centred in a removed glyph's box, and the glyphs
  centred there of a string operator whose codes there hold a removed code
  run (a duplicate, a second text layer, a copy at size 0 or invisible),
  because a copy left there leaves the word readable at the location; this
  is the location test of §18, so the write passes it.
- **An occurrence**: a text, its page and folding, and one or more
  narrowings (its index in `pdfkit_find`'s order, a rectangle it meets, the
  words before or after it, `docs/ABI.md` §4.11), resolved through the
  search `pdfkit_find` runs (`MatchFinder::locate`) to the glyph ranges it
  reports for that occurrence, then read as a glyph range. The narrowings
  must leave exactly one occurrence (`occurrence_not_found` and
  `occurrence_ambiguous` otherwise), because pdfkit applies no rule of its
  own to choose among them (CLAUDE.md §5).
- **A string occurrence**: one range of code points of one string of a
  marked-content property list that `pdfkit_find` reported (below), named by
  its page, the list's index on the page, the path from the list to the
  string and the range, with the document fingerprint; every other key of the
  report it carries must agree with the document. It is an expression at a
  location, as a glyph range is: it replaces that range by the placeholder in
  that string at that operator (§12) and is verified on its page (§18).
- **Search strings** with the caller's folding options, matched over each page's
  reading-order text, hidden blocks included. pdfkit folds only what the
  caller asks for (CLAUDE.md §5): every option below is off unless the caller
  turns it on, and with none on a string matches the text code point for code
  point. First, the caller's own rules, character by character, because
  which characters are the same text is the caller's decision:
  - `ignore`: the characters the caller lists read as absent, on both sides;
  - `equivalents`: each character of one of the caller's classes reads as the
    class's first character (an apostrophe class makes `O'Brien` match
    `O’Brien`); a character named twice, in two classes or in a class and in
    `ignore`, is refused, because it would read as two texts.
  Then the four options:
  - `case`: Unicode simple case folding (CaseFolding.txt status C and S);
  - `diacritics`: canonical decomposition (NFD) with combining marks removed;
  - `whitespace`: any run of white space, and every inferred word gap, equals one
    space; a search space also matches the absence of a gap between two words
    on different lines;
  - `hyphenation`: a joined hyphenation (§7) matches the word without its hyphen;
  - ligatures are already decomposed (§5.5);
  - under every option a positional presentation form (a decomposition
    UnicodeData.txt tags `<initial>`, `<medial>`, `<final>` or `<isolated>`:
    the Arabic presentation forms and their ligatures) reads as its letters,
    because it is the letter in the shape its place in the word takes and a
    reader that undoes the shaping (PDFium does) writes the letters; the other
    compatibility forms (a full-width letter, `ß`, a superscript digit) stay
    what they are.
  Folding builds a folded string with a map back to glyph indices, so a match in
  folded text is a glyph range. The ligature decomposition and the positional
  forms are no option: they are how the extraction reads the glyphs. pdfkit's
  own searches (the location test of a located match, the self-check's
  metadata originals, §18) fold everything, because they verify a removal
  rather than choose one. A search matches inside words ("Smith" is found in "Smithson"), because
  a redaction that misses a string inside a longer word leaks it; each page is
  searched on its own. A search also finds the glyphs whose code bytes spell
  the string (its UTF-8, Latin-1 or UTF-16BE form) inside one string operand
  where the text does not read it (a code no rung reads, a font whose
  `/Differences` name other glyphs, letters spread out of reading order),
  because an extractor reading the codes and the self-check's byte scan both
  find it there; these byte scans try every spelling the caller's classes
  allow, up to 64 per string, and skip no ignored character, because a
  producer writes the bytes of one spelling in one piece. For the same reason
  a text operator that shows no glyph (no
  font is current), an operator Annex A does not know, a comment and the bytes
  a lexer recovery skipped go from a content stream when they spell the
  string.

**Every reader's order.** A search also runs over the orders other readers put
a page's words in, because a redaction whose phrase match depends on one
reading order leaks the phrase to any reader that orders the words otherwise:
two blocks whose lines interleave, a superscript set between two lines, a
line that runs off the page into the hidden set, a word beside a right-to-left
run or the TeX logo's lowered E all split a phrase in pdfkit's order that a
reader of the drawing order reads whole, and a reader that keeps the page's
rows reads two words side by side across a column gap. The orders, over every
glyph of the page (the hidden ones included, a duplicate copy read with its
partner):

- the drawing order, the glyphs as the content streams show them;
- the rows read forwards and backwards: the lines of every block whose
  baselines agree within a fifth of the size (§7's rule for one line), in the
  reading order of their first lines, each read along its baseline from
  either end, so a right-to-left run reads in visual and in logical order.

In these orders a word gap is read from the positions: a space glyph, or a
glyph that starts more than 0.15 of the size past the end of the glyphs
before it (the smallest gap §7 measured between words); in the drawing order
also a glyph that starts more than 0.6 of the size back or more than half the
size off the baseline of the glyph before (a kern back and a lowered letter
stay in the word; a glyph drawn elsewhere starts one). Readers disagree on
the gaps they infer, so under the whitespace folding a space of the string
matches a word gap or none; inside a word of the string no gap is allowed,
so "AnnualReport" is not read across a column gap. A match found only in
another order removes the glyphs it covers: each stretch of them contiguous
in reading order is an occurrence, the first draws the placeholder and the
others are holes, and glyphs the reading order's matches cover are not found
twice. The hyphenation join is the reading order's alone.

A phrase is matched as a phrase: its words found one by one (each word of a
phrase present on the page within the phrase's span, in no order a reader
uses) are not a match, because over the dataset the orders above leave no
phrase for PDFium to find, while the word-by-word rule would remove or refuse
the common words of a title wherever they stand near each other (it fired in
42 of 812 metadata rewrites, on 317 pages, on no phrase a reader reads).

A match spans operators, fonts, `TJ` adjustments, lines and a hyphen, and is one
match. It removes its glyphs, every duplicate folded into one of them (§7), and
every glyph of an `/ActualText` span one of them belongs to, because the span's
text is read as one unit. Overlapping or adjacent matches with the same placeholder are merged;
with different placeholders, the earlier-starting one wins the overlap. Of
two that start on the same glyph, the one that ends later wins: it covers the
other whole, so the shorter draws nothing (the searches `Contoso` and
`Contoso Group` over "Contoso Group" draw the placeholder of `Contoso Group`
in either order). Of two that cover the same glyphs, the match recorded first
wins: a match of an earlier `pdfkit_redact_text` call before one of a later
call, and within a call the earlier entry of `matches`. The rule is fixed
rather than chosen per call, because a caller that resolves its own overlaps
first needs a determined outcome, not a particular one.
A string that matches nothing is reported with a count of zero, never as a
failure.

**Finding before editing.** `pdfkit_find` runs the search above and records
nothing: per value, every occurrence on the pages asked, so a caller shows the
counts and lets its user pick one occurrence to send back as a glyph range.
The search is one function for both callers (`MatchFinder::locate`), and a
redaction's search takes its occurrences from it, because a count shown to a
user that differs from the count a search removes is a lie. An occurrence
found in the reading order is one range; one found only in another order, or
in the codes of a string operand, keeps every stretch of the glyph list it
covers as one occurrence with several ranges, named by the first order that
found it (the codes of one operand are drawn in their order, so they report
the drawing order); the redaction takes the same stretches one by one, the
first drawing the placeholder. Its rectangles are its glyphs' boxes united per
reading line, and its context the page's reading-order words wholly before its
first glyph and wholly after its last. Its glued letters are the characters
of the words it lies inside that precede its first glyph and follow its last:
in the reading order, the glyphs of the word of `pdfkit_text` holding its
first glyph that come before it and of the word holding its last that come
after it; in another order, the glyphs that order reads next to it up to a
word gap or a white-space glyph on each side. pdfkit decides nothing with
them: whether an occurrence stands as a word is the caller's rule.

**Strings outside the page text.** A search also rewrites the strings of the
marked-content property lists (§12), which the write keeps and an extractor
copies, so `pdfkit_find` reports a value's occurrences in them apart from
the glyph occurrences (`strings`, `docs/ABI.md` §4.16), each a string
occurrence a caller can send back. The lists are those of every `BDC` and
`DP` of the page's walks (the page's content and every draw of a form),
inline or named through the walk's `/Properties`, numbered on the page in
walk order then operator order; an `/OC` list is left out, because every
write removes it (§15). Every string of a list at any depth (dictionaries and
arrays, references followed, at most 32 deep) is read as a text string; its
surface is `alt` for a top-level `/Alt`, `expansion` for `/E`, `actual_text`
for `/ActualText` and `property` for any other. An `/ActualText` that a
glyph reads as its text (the outermost one over a glyph, §6.1) is left out,
because the page text holds it and the glyph occurrences report it. The
occurrences are found by the search of a string that is no page text
(`MatchFinder::ranges_in`) under the value's folding, their offsets in code
points, and their glued letters are the string's characters next to the range
up to white space on each side.

## 12. Text surgery

The matched glyphs leave the content stream and every other glyph keeps the
position the original gave it (CLAUDE.md §5).

- **Inside `TJ`**: the removed codes' bytes leave the string operand; the hole
  becomes a number equal to the removed advance in thousandths of text space,
  `-(Σ tx) / (Tfs × Th) × 1000` with each `tx` from §6.1 (so `Tc`, `Tw` and the
  removed codes' own adjustments are included), merged with adjacent numbers.
- **Inside `Tj`, `'` and `"`**: the operand is cut at code boundaries and the
  operator rebuilt as the `TJ` of its kept pieces and the holes' numbers, so
  the codes after the match land where they were without a `Td` or `Tm`, which
  would also move the line matrix a later `T*`, `'` or `"` reads; `'` becomes
  `T*` and the `TJ`, and `"` becomes `Tw`, `Tc`, `T*` and the `TJ`, so the line
  advance and the spacing survive. An operator whose codes are all removed
  becomes the `TJ` of one number, which keeps the text position the operators
  after it start from. An operator whose codes cannot be placed on their
  operand's bytes loses every glyph it shows, because a glyph kept by mistake
  is a leak and one removed by mistake is not.
- Multi-byte codes are cut at code boundaries the font's codespace defines,
  never inside a code.
- **Forms**: a value's text inside a form XObject is rewritten inside the
  form, once, and the rewrite reaches every page that draws the form,
  because a value is removed wherever it occurs; MuPDF's filtered copy per
  use leaves the original form in the file, which the reachable-object
  sweep exists to prevent. A glyph range or an occurrence is one draw on
  one page: when its glyphs lie in a form drawn more than once in the
  written pages (a running header or footer on every page, or a form drawn
  twice on one), that draw's `Do` is renamed to a copy of the form that
  holds the edit (with the form's shared edits), a form nested in a form is
  copied along its path with each copy's `Do` renamed to the copy below it,
  and every other draw keeps the original, which still holds the text and is
  drawn elsewhere (`FormSplitter`). A form drawn once is edited in place,
  because a copy would leave the unedited original named in the resources.
  The draws are counted over the written pages from their `Do` operators and
  the forms' own.
- **Marked content**: on a span whose glyphs are all removed the `/ActualText`
  goes with them (the placeholder drawn for them is then read once) and an
  `/Alt` or `/E` becomes the placeholder; on any other span every occurrence of
  a redacted string inside an `/ActualText`, `/Alt` or `/E` is replaced by its
  placeholder, because these strings are what an extractor copies instead of
  the glyphs. Every other string of every `BDC` and `DP` property list (an
  artifact's `/Contents`, a producer's own key) has the redacted strings
  replaced too. The property list is rewritten inline in its operator or in
  the `/Properties` resource that names it.
- **A string occurrence** (§11) replaces its range by its placeholder in that
  string, before the values are replaced in the same list, so its offsets
  read the original; on a span whose glyphs are all removed the rule above
  still wins. Ranges on one string follow the overlap rule of §11, and two
  identical ones are one. Where the list lives decides how the edit stays at
  its place: a list inline in the page's content is rewritten in its
  operator; one inline in a form is rewritten in that draw's copy of the form
  when the form is drawn more than once (`FormSplitter`, as a located glyph
  edit is) and in the form itself otherwise; a list named through
  `/Properties` is rewritten where it lives (its object, or its entry in the
  resources) when every operator naming it in the written pages carries the
  same edit, and otherwise is written inline, with the edit and its values
  resolved, into the operators the edit names, the named original kept for
  the others. The draws are counted over the written pages when the plan is
  written, because a page selection changes them.
- Type 3 glyphs, render-mode-3 text and every hidden class are removed the same
  way; a duplicate's both operators are edited.
- **Clip text (render mode 7, and the clip half of modes 4 to 6)**: a glyph
  shown in a clipping mode adds its outline to the clip the text object sets
  at `ET` (ISO 32000-1 section 9.3.6, recalled: the outlines of a text object's
  clipping glyphs accumulate and are intersected with the clip at its end).
  Removing such a glyph removes its code like any other's and keeps its
  advance, so the outline leaves the clip; the placeholder over glyphs that
  are all invisible is drawn in mode 3 and adds nothing to it; nothing else
  in the stream changes. What that does to the page:
  - **Some clip glyphs of a text object removed**: the clip at `ET` is the
    union of the kept glyphs' outlines, so whatever is painted after `ET`
    until the `Q` that restores the clip (an image or a shading seen through
    the letters) is no longer painted inside a removed glyph's outline: the
    letter's shape disappears and what lies under it shows there.
  - **Every clip glyph of a text object removed**: each operator that showed
    them becomes the `TJ` of one number, so the text object shows no glyph in
    a clipping mode. PDFium (`CPDF_StreamContentParser::AddTextObject` and
    `Handle_EndText`, which adds a text clip only when a clipping glyph was
    shown) and pdf.js (`CanvasGraphics.endText`, which clips only when it
    collected a glyph path), both read in source, then leave the clip as it
    was before `BT`, so what is painted afterwards is painted in full over
    the area the letters restricted it to: an image seen only through a word
    shows whole. The page can show more than the original showed; pdfkit
    does not change this today (`OPEN-QUESTIONS.md` §1.2 asks the owner
    whether to close such a text object with an empty clip).
  `tests/unit/redact/clip_text_test.cpp` writes both cases.
- **Width side channel**: the following glyphs keep their positions, which
  leaks the removed span's width (CLAUDE.md §5); re-anchoring the rest of the
  line would be an opt-in switch and is never the default.
- **Across lines**: a match spanning a line break removes the glyphs on every
  line and draws the placeholder once, at the first line's first removed glyph;
  the later lines' holes stay holes; an attached hyphen goes with the match.
- A box drawn over live text is never a redaction, because the text stays in the
  file (the Manafort filing).

## 13. The placeholder font and the width-fitting rule

- **The font**: a `Type0` font over a `CIDFontType2` descendant, `Identity-H`
  encoding, `/CIDToGIDMap /Identity`, holding the 40 glyphs of the placeholder
  alphabet (`[`, `]`, `_`, `0`–`9`, `A`–`Z`) and `.notdef`, cut from Liberation
  Sans Regular and Bold (SIL OFL 1.1, whose FAQ permits embedding a subset) by a
  build tool in `build/fonts/` and committed as data pinned by hash, so the
  run-time writer subsets nothing; with a `/ToUnicode` stream so an extractor
  reads the placeholder, `/W` from the subset's `hmtx`, and a font descriptor
  with the subset's metrics. Liberation Sans is metric-compatible with Arial, so
  a placeholder's width is close to what the same letters took in a common
  business face. It is embedded once per output file, only when a placeholder is
  drawn, the bold face only when a bold placeholder is drawn.
- **Drawing**: the placeholder is drawn in its own `BT`/`ET` inside `q`/`Q`,
  placed after the `ET` that closes the text object holding the removed run's
  first glyph, in the same stream, so it is under the same CTM and clip; at
  that glyph's text matrix (origin and rotation), font size, `Ts` and `Tz`, with
  `Tc` and `Tw` zero, in its fill colour when that is a device colour (else the
  colour in force there), an invisible run's placeholder in render mode 3; in
  bold when the removed font's descriptor has the force-bold flag, a
  `/FontWeight` of 600 or more, or a name containing `Bold`, `Black` or `Heavy`.
  The face is named in the resources the stream reads under `PdfkitPH` and the
  first eight hexadecimal digits of its program's hash; a form without
  resources of its own has the face named in every kept page that draws it.
- **Fitting**: the hole is the removed glyphs' advance plus the white space up to
  the next kept glyph on the same line that is not a space (to the run's end
  when none follows), measured in device space along the run's direction.
  The placeholder is drawn at its natural width when it fits (`natural`); else
  scaled horizontally with `Tz` down to a floor of 60 percent (`scaled`); else
  at the floor, overflowing to the right (`overflow`), because an overflow is
  visible and a placeholder drawn over the next glyph hides a word the reader
  cannot see was there. The result of `pdfkit_redact_text` reports the fitting
  per occurrence.
- A placeholder with a character outside the alphabet is refused before
  anything is written.

## 14. Image treatments

A treatment applies to an image identity and reaches every occurrence, because
the samples belong to the object (one answer per picture): the image object
is decoded to its samples as its encoding stores them (before `/Decode` and
the colour space), treated, re-encoded and put back under its own object
number, so every draw of it, on every page and in every form or pattern,
shows the treated pixels and the original stream is gone from the output. An
overlay drawn over an image in the content stream is never a treatment,
because the samples stay in the file.

- **Box**: a filled rectangle painted into the samples in image pixel space,
  black by default or a caller colour given as red, green and blue; the fill
  is the sample value of the image's own colour space and `/Decode` that
  reads nearest to that colour (an Indexed image's nearest palette entry, the
  inverse of a `[1 0]` `/Decode`, a rich black in CMYK), found by a search
  over the sample values through the same conversion the decoder applies, and
  never a value a colour-key `/Mask` hides. A caller rectangle given in page
  space is mapped through the inverse of the CTM of every draw of the image on
  that page and rounded outward, and every mapped rectangle is painted. An
  optional caption from the placeholder alphabet is drawn inside the box with
  a bitmap font of the alphabet rasterised from Liberation Sans at build time
  (`build/fonts/bitmap_font.py`, heights 8, 12, 16, 24, 32, 48 and 64
  pixels) and committed as data, at the largest height whose line fits, in
  white or black, whichever contrasts with the fill, upright as the image's
  first draw shows it (an image drawn flipped or turned by a multiple of 90
  degrees gets its caption turned back); a box too small for 8 pixels draws
  none. A box over the whole image is the default rectangle. Region boxes,
  boxes over rectangles the caller found in the pixels, accumulate beside a
  box or a blur.
- **Blur**: a mosaic whose block is at least 8 pixels and at least a
  sixteenth of the image's smaller dimension (a multiple of 8 for a JPEG, so
  the re-encoded blocks stay constant), on a grid from the image's top-left
  corner, each block's part inside the rectangle filled with its mean (an
  Indexed image's palette entry nearest the mean colour), because a mosaic of
  that size holds no recoverable detail and a convolution blur is
  reversible; refused for a bitonal image whose smaller dimension is under
  128 pixels (box offered).
- **Remove**: every `Do` of the XObject is removed from every content stream
  (the page's, the forms it draws, the tiling patterns and soft-mask groups
  its resources name), the operator alone, so the `q`, `Q` and `cm` around it
  stay balanced; an inline image's `BI … EI` is removed; the XObject's entry
  goes from every resource dictionary walked and the object itself is
  emptied, so nothing left can reach its bytes, leaving its rectangle empty.
- **Soft mask**: the same rectangle is treated in the mask (`/SMask`, a
  stencil `/Mask`, or a JPEG 2000 codestream's alpha, which becomes a soft
  mask of its own), set opaque under a box and mosaicked under a blur, because
  a box over the colour and not over the mask shows the box's edge through the
  mask's shape; a mask of another size is treated over the same part of the
  picture, and a mask that does not decode is dropped.
- **Re-encoding**: a treated lossless image is re-encoded with Flate in its
  source colour space and bit depth (the `/DecodeParms` and predictors
  dropped); a treated `DCTDecode` image is re-encoded as JPEG through
  libjpeg-turbo at quality 90 (author decision) with its component count and
  no chroma subsampling, CMYK written with the Adobe marker; a treated JBIG2,
  CCITT or JPX image is re-encoded as Flate (1-bit for a bitonal source, whose
  codec output is already the image's samples). `/Decode` and `/ColorSpace`
  are kept so the pixels mean what they meant, except that a JPX image's
  `/Decode`, which a reader ignores, is dropped, and a `/ColorSpace` the codec
  implied is written out. A treated inline image is written back in its
  content stream with its abbreviated keys and the one filter `/AHx` over its
  samples as they are (a JPEG source included), whose hexadecimal data cannot
  hold the `EI` that ends it, and one filter is the form every reader of
  inline images handles.
- A treatment that writes exactly an image's original bytes (a blur of an
  image whose blocks were already constant) hides nothing and is not a
  survivor.
- An image the library cannot decode accepts keep and remove only.

## 15. The channel table

The channels are listed in one source file, `src/writer/channel_table.cpp`, one
row per channel, each row naming the report key of `pdfkit_channels`, how the
channel is detected and what the writer strips, with a planted-marker unit test
per row in `tests/unit/writer/channel_table_test.cpp`, because the
public record of redaction failures is a record of channels nobody listed and a
channel added later must be a row with a test, not a sentence. The rows:

| Channel | What the writer does | Why |
|---|---|---|
| earlier revisions (`/Prev` chains, incremental updates) | gone: the output is a fresh file | a redaction saved incrementally leaves the original in front of it |
| unreferenced objects | gone: only reachable objects are written | a deleted page's text stays until a full rewrite |
| page thumbnails `/Thumb` | removed from every page | a thumbnail is a picture of the page before the edit |
| `/PieceInfo` (document and page) | removed | private application data can hold the whole source |
| `/LastModified` | removed | a timestamp of the producer's edit |
| object-level `/Metadata` on pages, images, forms and fonts | removed; only the catalog's packet is kept (§10) | each is an XMP packet that can name people |
| JavaScript: the `/JavaScript` name tree, `/JS` actions, `/AA` on catalog and pages, `/OpenAction` | removed | a script can hold text and nothing the caller needs runs |
| embedded files: `/EmbeddedFiles`, `/AF` associated files, file-attachment annotations | removed | a whole client document can hide in one |
| annotations: every `/Annots` entry, widget, link, markup, popup, stamp, FreeText and their appearance streams | removed from every page; `/Redact` annotations present in the input are counted as `pending_redactions` in the channels report | a comment names people and a stamp's appearance is a content stream outside the catalogue |
| interactive forms: `/AcroForm` with field values, `/XFA` and `/NeedAppearances` | removed (with the widget annotations above) | a form's data is outside the page content |
| outlines `/Outlines` | removed | a bookmark titled with a name is the name |
| named destinations `/Dests` and every `/Names` tree | removed | destination names name things |
| the structure tree `/StructTreeRoot`, `/MarkInfo`, page `/StructParents`, `/Tabs` | removed | `/ActualText`, `/Alt`, `/E` and `/T` strings are read aloud and copied out |
| optional content: `/OCProperties`, every `/OC` key, and the content of groups turned off | the content the default configuration (`/D` with its `/BaseState`, `/ON` and `/OFF`) turns off is removed by `redact::OptionalContentRemover` before the strip: an `/OC` marked-content sequence whose group or membership dictionary is off draws nothing (inside it the operators that set the graphics state stay, a path that paints goes, or keeps its construction ended by `n` when it clips, a text-showing operator becomes a `TJ` moving the position by its glyphs' advance, and `Do`, `sh`, inline images and marked-content operators go), and a `Do` of a form or image XObject whose `/OC` is off goes with the XObject's resource entries, so the reachable sweep drops it; the `/OC` marks of a sequence that is on go and its content stays; then `/OCProperties` and every `/OC` key are removed, with the `/Properties` entries naming groups. A catalog without `/OCProperties` shows every group, so only the marks go (owner decision) | a viewer hides switched-off content and every extractor reads it; group names can name things |
| page labels `/PageLabels` | removed | a label can carry a name or a reference |
| XMP history `xmpMM:History`, identifiers | removed with every other `xmpMM` (media management) property of the catalog's packet, the rest of the packet kept byte for byte; a packet that does not decode is dropped (`xmp_unreadable`); `xmpMM:DocumentID` and `xmpMM:InstanceID` are kept only holding the placeholder the metadata writer puts there, which the file writer patches with the output's identifier (§10) | a history names the operator and software of every save |
| article threads `/Threads` and page beads `/B` | removed | a thread title names things |
| web capture `/SpiderInfo` and page `/ID` | removed | a capture records source URLs |
| portfolios `/Collection` | removed (the cover document's pages are kept) | a portfolio is a container of other documents |
| `/Perms`, `/Legal` | removed | signatures and legal attestations do not survive an edit |
| `/Encrypt` | removed: output unencrypted (§2) | the original's restrictions were set for the original |
| `/URI` base dictionary | removed | a base URL names a site |
| search indexes (`/PageMode` embedded index, `/SearchIndex` and similar private keys) | removed | an index holds every word of the document |
| private keys of form and image XObjects | removed: a form keeps only the keys of ISO 32000-1 tables 5 and 95, an image those of tables 5, 89 and 146 (`/Ref`, `/OPI`, a web-capture `/ID` and the external-file keys go too) | a producer's own key on a form or an image (a pdfTeX source file name, an editor's private copy of an annotation's text) is a channel nobody listed; the text redaction's self-check found both in the dataset |
| fonts, output intents, colour spaces, ExtGState, shadings, patterns | kept as they are | they draw the page and name no text (a removed glyph's width and ToUnicode entries name no text either) |

Catalog keys not named here are kept only from an allow-list (`/Type`,
`/Pages`, `/Version`, `/OutputIntents`,
`/ViewerPreferences` with its standard keys only, `/PageMode` (`/UseNone` and
`/FullScreen` kept; any other mode, whose panel the output does
not have or which names a private index, read as `/UseNone`, and
`/NonFullScreenPageMode` likewise), `/PageLayout`, `/Lang`, `/Metadata` as
above, `/Extensions`),
and page keys likewise (`/Type`, `/Parent`, `/Resources`, `/MediaBox`,
`/CropBox`, `/BleedBox`, `/TrimBox`, `/ArtBox`, `/Rotate`, `/Contents`,
`/Group`, `/UserUnit`, `/VP`, `/BoxColorInfo`, `/Trans`, `/Dur`), because a
channel nobody listed is a channel that leaks (author decision: an allow-list,
not a deny-list). `/PieceInfo`, `/LastModified`, `/AA`, `/JS`, `/AF`, `/EF`,
`/StructParent` and `/StructParents` are removed from every dictionary the
output holds, and `/Metadata` from every dictionary but the catalog, because
forms, images and fonts carry them too. Each row's id is the dataset's
taxonomy tag of the channel (`ch_thumbnail` and so on), so a report, a tag and
a planted-marker file name the channel alike. The Info dictionary is not a
channel: it is copied as it is until the metadata writer rewrites it (§10).

## 16. Page selection and the leak guarantee

- The writer rebuilds the page tree from the kept pages in the caller's order
  under one fresh `/Pages` node, copying each page's inherited `/Resources`,
  `/MediaBox`, `/CropBox` and `/Rotate` onto the page, because the old
  intermediate nodes go.
- **Resource pruning**: each kept page's resource dictionary, and recursively the
  resources of every form, tiling pattern and soft-mask group it draws, is
  pruned to the names its content actually uses, writing a pruned copy where
  the original dictionary is shared, because an inherited `/Resources`
  routinely lists XObjects and fonts only removed pages draw, which would
  otherwise stay reachable and carry the removed pages' content. A form, a
  Type 3 glyph procedure, a pattern or a group without resources of its own
  adds its names to its user's dictionary; `/DefaultGray`, `/DefaultRGB` and
  `/DefaultCMYK`, which no operator names, and `/ProcSet` are always kept; a
  key outside the seven resource categories is dropped; a Type 3 font's own
  resources are kept whole; a content stream that does not read whole keeps
  its dictionary whole and is counted. Every write, a write without a
  selection included, rebuilds the flat tree and prunes, so a resource no page
  draws is never written.
- Every document-level structure that names a page is already removed by the
  channel table (§15), so nothing outside the page tree points at a removed
  page, and optional content goes whole after its switched-off content.
- What only a removed page reached (its content streams, its forms and images,
  its annotations, its thumbnail) is unreachable and is not written (§17).
- **The leak test**: a fixture written in code holds a unique marker in every
  channel a page owns or names (its content stream, a form only it draws, an
  image only it draws, an entry in a resource dictionary shared through
  inheritance, an annotation and its popup, a thumbnail, an incremental update
  that revised it, an unreferenced object, a structure node with `/ActualText`,
  a page label, an optional-content group only it uses); every proper subset of
  its pages is selected, and the test asserts that no removed page's marker
  occurs anywhere in the output's bytes with every stream decompressed, that
  every kept page's content marker is present, that every reference resolves,
  that the output re-opens with the asked page count, and that each kept page's
  untouched content streams are byte-identical to the source's decoded content.

## 17. The writer

- **A fresh file only**, never an incremental update (CLAUDE.md §5).
- **Reachable objects only**: a traversal from the new trailer (`/Root` and,
  when metadata is written, `/Info`) after the edits and the channel table;
  nothing else is written.
- **Deterministic renumbering**: objects are numbered 1, 2, … in first-reach
  order of a depth-first traversal that visits dictionary entries in their
  stored order and array items in order, so one input and one edit plan give one
  output byte for byte.
- **Header**: `%PDF-` with the larger of the input's version (header or catalog
  `/Version`) and 1.5 (the cross-reference stream's minimum), followed by a
  binary comment line of four bytes above 127.
- **Objects**: every object is written as an indirect object of its own; no
  object streams are written (author decision: simpler writer, and an untouched
  object's bytes stay findable). Dictionaries keep their read key order, strings
  their read form (literal, with `(`, `)`, `\` and the non-printable bytes
  escaped, or hex), integers as integers and reals in the shortest form that
  reads back to the same value, with a decimal point and no exponent (the
  object model keeps a number's value, not its text, so `0.50` is written
  `0.5`).
- **Streams**: an untouched stream is copied with its original `/Filter`,
  `/DecodeParms` and encoded bytes (decrypted first when the input was
  encrypted, the filters kept), its `/Length` rewritten as a direct integer; a
  rewritten stream (a page's or form's edited content, a treated image, the
  rewritten XMP, the placeholder font data) is Flate-compressed, except a
  re-encoded JPEG (§14) and the XMP packet, which is written uncompressed so
  tools that scan for it find it.
- **Cross-reference**: one cross-reference stream (`/Type /XRef`, `/W [1 n 2]`
  with `n` the narrowest width that holds the largest offset, `/Index`,
  `/Size`, `/Root`, `/Info`, `/ID`), Flate compressed, then `startxref` and
  `%%EOF`.
- **`/ID`**: both elements are the first 16 bytes of the SHA-256 of the output
  written with a zero placeholder `/ID`, then patched in place, so the
  identifier is a function of the content.
- **Unencrypted** always (§2).
- The output re-opens in pdfkit with no repair and in PDFium; the dataset tier
  asserts the first and the PDFium tier (`compare.py --check write`) the
  second.

## 18. The self-check

Before `pdfkit_write` hands an output back, the library re-reads it with its own
reader and verifies each request where it applies, because pdfkit is a
literal tool (CLAUDE.md §5): a value removed everywhere is verified
everywhere, an expression removed at a location is verified there, and a
rewritten metadata field is verified in the metadata.

| Request | Removed from | Verified on |
|---|---|---|
| a search string (a value) | every occurrence in every order of every page (§11) | every surface below, under the string's folding |
| a glyph range or an occurrence (an expression at a location) | the glyphs of its ranges and every copy of its text drawn at the place (§11); inside a form drawn elsewhere too, from a copy of the form that this draw alone reads (§12) | that location: no occurrence of its text, in the reading order or any other order of §11, has a glyph (outside the placeholder font) whose box's centre lies in the box of a glyph the match removed; no string operator drawing a glyph there holds a code run the answer reported under `codes` (at a code boundary); no `/ActualText`, `/Alt` or `/E` of a span over the place (one whose `BDC` ... `EMC` encloses a glyph there, the placeholder included, or whose `/ActualText` a glyph there names) holds the text. Nothing elsewhere is searched for it |
| a string occurrence (an expression in one marked-content string) | its code points in that string, at that operator alone (§12) | that page: its marked-content strings (§11), read with no folding and the placeholders cut out, hold its text fewer times than the input page's did by at least the number of distinct string occurrences of that text removed there. Nothing elsewhere is searched for it |
| a rewritten metadata field | the Info entry and its paired XMP properties | each of its source values (the Info value, each paired XMP value and array item), of any length, with every folding on, in the Info dictionary, the XMP packet and every string object outside content streams; the page text is not searched for it, because the document may say it for another reason (an author "PC" in a page about a PC) |

The location is the removed glyphs' own boxes and a glyph is there by its
centre, because a kept neighbour's box touches a removed one while its centre
lies outside, and a second text layer drawn over the word lies inside; the
answer's `rectangles` run over the rest of the line and the placeholder, so a
second occurrence on the same line would lie in them. A value whose search
the caller also adds with every folding on is verified everywhere as a
value, whatever else the plan holds. The values the caller writes into the
metadata fields are cut out of the metadata surfaces before the originals are
searched, as the placeholders are, because a written value holding an
original ("[_5_]" over "5") is the caller's own.

The surfaces of a value:

- the glyph text of every page in reading order and in every other order a
  reader uses (§11: the drawing order and the rows read from either end),
  hidden blocks included, because an output proven clean in one order is
  not clean for a reader that orders the words otherwise, and the redaction
  removes what any of these orders reads, so a write never ends in a refusal
  for a phrase the search could have matched (an occurrence drawn wholly in
  the placeholder font is the placeholder, and the other orders leave the
  placeholder font's glyphs out, because a placeholder squeezed into or
  overflowing its hole lies among the kept glyphs along the baseline and its
  letters are no text of the document);
- every `/ActualText`, `/Alt` and `/E` in marked content;
- the Info dictionary's strings (those of an array or dictionary value
  included) and the XMP packet's text (its character data and attribute
  values, entities read, the namespace declarations' URIs left out, because
  they are the format's vocabulary);
- every string object reachable in the file, less the strings of font
  dictionaries (a CIDSystemInfo's registry and ordering), which are the font
  format's vocabulary;
- the decompressed bytes of every stream, as a raw byte scan of the UTF-8, the
  PDFDocEncoding and the UTF-16BE forms of the string (ASCII letters folded
  when the string folds case), because a channel the table missed shows here
  first: a content stream (page contents, a form, a pattern, a Type 3 glyph)
  by the bytes of its string operands, its comments, the bytes its lexer
  skipped and the operators Annex A does not know, because its operator
  names, resource names and marked-content tags are the producer's
  vocabulary; every other
  stream whole, except font programs and a font's CMaps, image samples, ICC
  profiles, functions and the XMP packet (searched as text above).

Every value was searched on every page by the same reader when its match was
added, so the page text is read again on the pages the redaction changed (an
edit, or an edited form drawn) and on the page of every located match; an
unchanged page reads as it did. Every other surface is searched on every
page.

Before a surface is searched every placeholder of the plan is cut out of it,
so a placeholder holding a redacted string is not a survivor.

A survivor is a refusal (exit code 3 in the CLI) naming the page and the surface
for each occurrence (`location` for a glyph range's text at its location); no
output is handed back. The re-read is bounded by the same limits as any open.
`pdfkit_check` runs the search of a value over any bytes the caller hands it.

## 19. Refusals and warnings

Every refusal carries the three fields of CLAUDE.md §3.3 (`failed`,
`expected`, `fix`) under its code.

| Refusal code | When | The message names |
|---|---|---|
| `not_pdf` | no `%PDF-` in the first 1,024 bytes and no page object found by the scan | that the bytes are not a PDF or are truncated, and to re-export the file |
| `no_page` | a PDF header but no object with `/Type /Page` | that the file is probably truncated, and to re-export it from its application |
| `password_required` | the user password is not empty, or a revision 5 or 6 `/Perms` does not decrypt with the file key | that a password is needed, and to remove it in the PDF application |
| `unsupported_security_handler` | `/Encrypt` names a handler other than `/Standard`, a revision outside 2 to 6, a version outside 1, 2, 4 and 5, or a crypt filter or method the handler does not define | the handler and revision found, and to remove the protection in the producing application |
| `limit_exceeded` | a stream, the document, an image or an allocation past a limit of §3.2 | the object, the limit, and to re-save or split the file or raise the limit at open |
| `page_ceiling` | the page count is above the configured ceiling | the count and the ceiling, and to split the file or raise the ceiling at open |
| `undecodable_image` | `pdfkit_image_data` asked for an image whose encoding does not decode, or `pdfkit_redact_images` for a box, blur or region box of it (the call is refused, the document is not) | the identity, the encoding, the object and the fault, and that only keep and remove apply |

| Warning code | When | The message names |
|---|---|---|
| `repaired` | the file was read by reconstruction (§1.6) or an object needed recovery | each reason, once |
| `damaged_stream` | a stream decoded only up to a fault | the object and the page, and that the page was read as far as it decodes |
| `unreadable_text` | a page holds codes no rung could read | the page, the count and the font names |
| `hidden_text` | a page holds glyphs of a hidden class | the page and the count per class |
| `font_not_embedded` | a page uses a font with no program and no `/Widths` outside the Core 14 aliases | the page and the font name, and that widths are estimated |
| `undecodable_image` | an image's encoding failed to decode | the identity, the encoding and the fault, and that only keep and remove apply |
| `image_in_pattern` | an image is drawn inside a pattern or a soft mask | the identity, and that it is listed but not numbered |
| `xmp_unreadable` | the XMP packet does not scan | that the packet is dropped from any output |
| `pending_redactions` | the input holds `/Redact` annotations never applied | the page and the count, and that the marked text is still in the input |
| `resource_missing` | a content stream names a resource that resolves to nothing | the page and the name |
