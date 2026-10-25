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
  hashed with the key salt; `/Perms` is checked and a mismatch is a warning, not
  a refusal. SASLprep reduces to nothing for the empty password.
- **Crypt filters**: `/CF` with `/StdCF` (`/CFM` `/V2` RC4, `/AESV2`, `/AESV3`),
  `/StmF` and `/StrF` choosing them, `/Identity` meaning no decryption, a stream
  whose own `/Filter` names `/Crypt` with `/Name` using that filter.
- **What is not decrypted**: the cross-reference stream, the strings inside an
  object stream (the stream as a whole is decrypted), the `/Encrypt` dictionary's
  own strings, the `/ID` array, and the XMP metadata stream when
  `/EncryptMetadata` is false.
- **Refusals**: a non-empty user password is `password_required`; a handler other
  than `/Standard` (a public-key or vendor handler) is
  `unsupported_security_handler` (§19).
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
  font with no `/ToUnicode` is unreadable and counted (CJK data is a later data
  addition). Its codes are split by the codespace its name implies (the
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

- Graphics state: `q` and `Q` (a stack with a depth limit of 256; an unmatched
  `Q` is ignored, an unclosed `q` is closed at the end), `cm`, `w`, `J`, `j`,
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
  between the first's advance end and the second's origin is under 0.3 of the
  font size; two words up to 2.0 of the font size; beyond that, a block
  boundary candidate. A decoded space or a `TJ` adjustment wider than the word
  gap both end a word, because a space glyph is routinely absent and kerning
  numbers routinely split words. These thresholds lie inside the range PDFBox
  and pdfminer use.
- **Lines**: glyphs whose baselines agree within a fifth of the font size, in
  the baseline's own direction, so rotated text forms its own lines along its
  rotation; vertical text (`Identity-V` or a text matrix rotated by 90°) forms
  vertical lines read top to bottom.
- **Direction**: a line whose strong characters are right-to-left (Hebrew,
  Arabic) is ordered by logical order as the content drew the codes when the
  producer drew them in logical order, and reversed from visual order otherwise
  (author decision: the drawing order wins when no strong-character analysis
  says otherwise); the line carries its direction.
- **Blocks and order**: a recursive XY-cut over the lines' boxes, with the
  Docstrum nearest-neighbour refinement for columns of uneven spacing, both
  ported from PdfPig's page segmenters (Apache 2.0, the port's header names the
  source). A tagged PDF's structure order is used instead when `/StructTreeRoot`
  is present and every marked-content item it orders resolves to glyphs of the
  page, because it is the producer's own statement of reading order.
- **Hyphenation**: a line ending in a hyphen (U+002D or U+00AD) followed by a
  line starting with a lower-case letter is one word across the two lines, the
  hyphen attached to the first half, so a match finds the word and a redaction
  removes the hyphen; a hyphen followed by a capital is kept, because
  "Jean-"/"Pierre" is a name. The same rule joins across a page break only when
  both pages are extracted together.
- **Duplicates**: a glyph with the same code and font as an earlier glyph whose
  box lies within a tenth of the font size of it (a fake bold or a shadow) is
  folded into the earlier record, which keeps both operators, so text is not
  doubled and a redaction removes both.
- **Determinism**: every sort is stable and every geometric key is rounded to
  0.01 pt (CLAUDE.md §9); the PdfPig nearest-neighbour word extractor's
  documented tie-order defect is not carried over.
- **Output shape**: per page, blocks in reading order, each with lines, each
  with words; every word, line and block carries its box and the half-open range
  of glyph indices (in the page's record order) it covers, so a match found in
  text maps back to records without a second reader.

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
| `covered` | an image or an opaque filled figure drawn later wholly covers the glyph's box |

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
| `DCTDecode` | libjpeg-turbo; CMYK and YCCK JPEGs decoded to CMYK, an Adobe APP14 marker meaning inverted CMYK values, as every viewer reads them |
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
  per-image cache of evaluated inputs). CMYK becomes RGB by the naive complement
  (`R = (1-C)(1-K)`), stated, because no colour management is vendored.
- Masks: `/SMask` (a grey image, with `/Matte` un-premultiplied), `/Mask` as a
  colour-key range array or as a stencil image; a mask whose size differs from
  the base is kept at its own size.
- Every decode checks width × height × components × bits against the budget
  before allocating (§3.2).

### 9.5 Extraction output

`pdfkit_image_data` answers, per identity: for a `DCTDecode` source, the
original JPEG bytes unchanged, because re-encoding a JPEG loses quality and adds
nothing; for every other source, a PNG (greyscale or RGB, 8-bit, or 16-bit when
the source is 16-bit) written by the library's own PNG writer over zlib; the mask
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
  UTF-8 when it starts with `EF BB BF` (PDF 2.0), PDFDocEncoding otherwise; dates
  are kept as written and also normalised to ISO 8601 when they parse. The XMP
  packet is the catalog's `/Metadata` stream, scanned as XML for the named
  elements (`rdf:Alt` taking `x-default` first, `rdf:Seq` and `rdf:Bag` items
  joined with `; `).
- **Side by side**: one row per pair, with both values and whether they agree,
  because viewers show whichever they prefer and a file whose two statements
  disagree says two things.
- **Writing**: a field the caller sets is written to both places; a field not
  set keeps its value in both. Info strings are written as UTF-16BE with BOM
  when not representable in PDFDocEncoding. The XMP packet is edited in place:
  the text of the named elements is replaced (XML-escaped) and every other byte
  kept, so custom namespaces survive; a packet that will not scan is dropped with
  `xmp_unreadable` rather than guessed at. `xmpMM:History` is removed, and
  `xmpMM:DocumentID` and `xmpMM:InstanceID` are regenerated, as is the trailer
  `/ID`, from the SHA-256 of the output's content (§17), because the original's
  identifiers identify the original.

## 11. Matching

A match (CLAUDE.md §4) arrives in one of two forms, each with its placeholder:

- **Glyph ranges** from an earlier extraction: page and half-open glyph-index
  range, with the document fingerprint (the SHA-256 of the input bytes) the
  extraction reported; a range from another document or out of range is refused,
  because a stale range redacts the wrong glyphs.
- **Search strings** with the caller's folding options, matched over each page's
  reading-order text, hidden blocks included:
  - `case`: Unicode simple case folding (CaseFolding.txt status C and S);
  - `diacritics`: canonical decomposition (NFD) with combining marks removed;
  - `whitespace`: any run of white space, and every inferred word gap, equals one
    space; a search space also matches the absence of a gap between two words
    on different lines;
  - `hyphenation`: a joined hyphenation (§7) matches the word without its hyphen;
  - ligatures are already decomposed (§5.5).
  Folding builds a folded string with a map back to glyph indices, so a match in
  folded text is a glyph range.

A match spans operators, fonts, `TJ` adjustments, lines and a hyphen, and is one
match. Overlapping or adjacent matches with the same placeholder are merged;
with different placeholders, the earlier-starting one wins the overlap.
A string that matches nothing is reported with a count of zero, never as a
failure.

## 12. Text surgery

The matched glyphs leave the content stream and every other glyph keeps the
position the original gave it (CLAUDE.md §5).

- **Inside `TJ`**: the removed codes' bytes leave the string operand; the hole
  becomes a number equal to the removed advance in thousandths of text space,
  `-(Σ tx) / (Tfs × Th) × 1000` with each `tx` from §6.1 (so `Tc`, `Tw` and the
  removed codes' own adjustments are included), merged with adjacent numbers.
- **Inside `Tj`, `'` and `"`**: the operand is cut at code boundaries; the codes
  before the match stay in the original operator; the codes after it are
  re-anchored by a `Tm` (or `Td` when the matrix is a pure translation) computed
  by the state machine at the first kept code, so they land where they were;
  `'` keeps its line advance and `"` keeps its `Tw` and `Tc` setting on the
  first piece. An operator whose codes are all removed is removed with its
  operands, except that `'` and `"` become `T*` (and `Tw`, `Tc`) so the line
  matrix still advances.
- Multi-byte codes are cut at code boundaries the font's codespace defines,
  never inside a code.
- **Forms**: text inside a form XObject is rewritten inside the form, once, and
  the rewrite reaches every page that draws the form, because the glyphs belong
  to the form and a match is a property of those glyphs; MuPDF's
  filtered-copy-per-use leaves the original form in the file, which the
  reachable-object sweep exists to prevent.
- **Marked content**: an `/ActualText`, `/Alt` or `/E` value on a span covering
  a removed glyph is replaced by the placeholder text when the span is wholly
  removed and otherwise rebuilt from the span's remaining decoded text with the
  placeholder in the removed part, because these strings are what an extractor
  copies instead of the glyphs.
- Type 3 glyphs, render-mode-3 text and every hidden class are removed the same
  way; a duplicate's both operators are edited.
- **Width side channel**: the following glyphs keep their positions, which
  leaks the removed span's width (CLAUDE.md §5); re-anchoring the rest of the
  line is a later switch and never the default.
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
- **Drawing**: the placeholder is drawn in its own `BT`/`ET` inside `q`/`Q`, at
  the removed run's first glyph origin, font size, text matrix rotation, `Ts`
  and `Tz`, in its fill colour (an invisible run's placeholder is drawn in
  render mode 3); in bold when the removed font's descriptor has the force-bold
  flag, a `/FontWeight` of 600 or more, or a name containing `Bold`.
- **Fitting**: the hole is the removed glyphs' advance plus the white space up to
  the next kept glyph on the same line (to the line's end when none follows).
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
the samples belong to the object (one answer per picture). An overlay drawn over
an image in the content stream is never a treatment, because the samples stay in
the file.

- **Box**: a filled rectangle (black by default, a caller colour in the image's
  colour space otherwise) painted into the decoded samples in image pixel space;
  a caller rectangle given in page space is mapped through the inverse of the
  occurrence's CTM and the union over occurrences is painted. An optional caption
  from the placeholder alphabet is drawn inside the box with a bitmap font of the
  alphabet rasterised from Liberation Sans at build time by a tool in
  `build/fonts/` and committed as data, scaled to fit the box. A box over the
  whole image is the default rectangle.
- **Blur**: a mosaic over the rectangle (the whole image by default) whose block
  is at least 8 pixels and at least a sixteenth of the image's smaller
  dimension, each block filled with its mean, because a mosaic of that size holds
  no recoverable detail and a convolution blur is reversible; refused for a
  bitonal image whose smaller dimension is under 128 pixels (box offered).
- **Remove**: the XObject is gone and every `Do` of it is removed from every
  content stream (an inline image's `BI … EI` is removed), leaving its rectangle
  empty.
- **Soft mask**: the same rectangle is treated in the mask (set opaque under a
  box, mosaicked under a blur), because a box over the colour and not over the
  mask shows the box's edge through the mask's shape.
- **Re-encoding**: a treated lossless image is re-encoded with Flate in its
  source colour space and bit depth (the `/DecodeParms` and predictors dropped);
  a treated `DCTDecode` image is re-encoded as JPEG through libjpeg-turbo at
  quality 90 (author decision) in its colour space; a treated JBIG2, CCITT or
  JPX image is re-encoded as Flate (1-bit for a bitonal source). `/Decode` and
  `/ColorSpace` are kept so the pixels mean what they meant.
- An image the library cannot decode accepts keep and remove only.

## 15. The channel table

The channels are listed in one source file, `src/writer/channel_table.cpp`, one
row per channel, each row naming what the writer does, the report key of
`pdfkit_channels` and the planted-marker unit test that proves it, because the
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
| optional content `/OCProperties` | kept minimal: groups renamed `OC1` to `OCn` in a stable order, the default configuration's ON/OFF state and `/Order` kept, `/Usage`, `/Intent` and alternate configurations dropped (author decision) | removing it would make OFF layers visible; group names can name things |
| page labels `/PageLabels` | removed | a label can carry a name or a reference |
| XMP history `xmpMM:History`, identifiers | removed with every other `xmpMM` (media management) property of the catalog's packet, the rest of the packet kept byte for byte; a packet that does not decode is dropped (`xmp_unreadable`); the identifiers are regenerated by the metadata writer (§10) | a history names the operator and software of every save |
| article threads `/Threads` and page beads `/B` | removed | a thread title names things |
| web capture `/SpiderInfo` and page `/ID` | removed | a capture records source URLs |
| portfolios `/Collection` | removed (the cover document's pages are kept) | a portfolio is a container of other documents |
| `/Perms`, `/Legal` | removed | signatures and legal attestations do not survive an edit |
| `/Encrypt` | removed: output unencrypted (§2) | the original's restrictions were set for the original |
| `/URI` base dictionary | removed | a base URL names a site |
| search indexes (`/PageMode` embedded index, `/SearchIndex` and similar private keys) | removed | an index holds every word of the document |
| fonts, output intents, colour spaces, ExtGState, shadings, patterns | kept as they are | they draw the page and name no text (a removed glyph's width and ToUnicode entries name no text either) |

Catalog keys not named here are kept only from an allow-list (`/Type`,
`/Pages`, `/Version`, `/OCProperties` as above, `/OutputIntents`,
`/ViewerPreferences` with its standard keys only, `/PageMode` (`/UseNone`,
`/FullScreen` and `/UseOC` kept; any other mode, whose panel the output does
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
  page; `/OCProperties` keeps only groups a kept page's content references.
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
  asserts both.

## 18. The self-check

Before `pdfkit_write` hands an output back, the library re-reads it with its own
reader and searches every text surface for every redacted string, under each
string's folding options (for glyph-range matches, the matched text as
extracted is the string):

- the glyph text of every page in reading order, hidden blocks included;
- every `/ActualText`, `/Alt` and `/E` in marked content;
- the Info dictionary and the XMP packet;
- every string object reachable in the file;
- the decompressed bytes of every stream, as a raw byte scan of the UTF-8, the
  PDFDocEncoding and the UTF-16BE forms of the string, because a channel the
  table missed shows here first.

A survivor is a refusal (exit code 3 in the CLI) naming the page and the surface
for each occurrence; no output is handed back. The re-read is bounded by the
same limits as any open. `pdfkit_check` runs the same search over any bytes the
caller hands it.

## 19. Refusals and warnings

Every refusal carries the three fields of CLAUDE.md §3.3 (`failed`,
`expected`, `fix`) under its code.

| Refusal code | When | The message names |
|---|---|---|
| `not_pdf` | no `%PDF-` in the first 1,024 bytes and no page object found by the scan | that the bytes are not a PDF or are truncated, and to re-export the file |
| `no_page` | a PDF header but no object with `/Type /Page` | that the file is probably truncated, and to re-export it from its application |
| `password_required` | the user password is not empty | that a password is needed, and to remove it in the PDF application |
| `unsupported_security_handler` | `/Encrypt` names a handler other than `/Standard`, or a revision outside 2 to 6 | the handler and revision found, and to remove the protection in the producing application |
| `limit_exceeded` | a stream, the document, an image or an allocation past a limit of §3.2 | the object, the limit, and to re-save or split the file or raise the limit at open |
| `page_ceiling` | the page count is above the configured ceiling | the count and the ceiling, and to split the file or raise the ceiling at open |

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
