# ABI.md

This file owns the C ABI of pdfkit: every exported symbol, its inputs and
outputs, its status codes, the JSON error object, the versioning rule, and the
command-line tool's verbs and exit codes. The header is
`include/pdfkit/pdfkit.h`; `CLAUDE.md` §2 owns which functions exist, and this
file owns how they are called. A function's JSON shapes are added here by the
change that implements it.

## 1. Calling rules

- **Strings** are UTF-8 and NUL-terminated. A JSON argument is a JSON object
  read strictly (RFC 8259: no comments, no trailing commas, no duplicate keys,
  well-formed UTF-8, nesting at most 64 deep, at most 64 MiB).
- **Sizes** are `size_t`; integers in JSON keep their exact value up to 2^63.
- **Handles** are opaque pointers: `pdfkit_document*` from `pdfkit_open`,
  released by `pdfkit_close`; `pdfkit_edit*` from `pdfkit_edit_new`, released
  by `pdfkit_edit_free`, always before its document is closed.
- **Threading**: a handle is used by one thread at a time; distinct handles are
  independent. The library holds no global mutable state, so this is the whole
  contract.
- **Buffers**: every function returning `pdfkit_status` writes its result, or on
  failure its JSON error object, into the `pdfkit_buffer` the caller passes.
  The caller declares the buffer zeroed (`pdfkit_buffer out = {0};`), passes its
  address, reads `data` and `size`, and frees it with `pdfkit_buffer_free`,
  which zeroes it again. `data` is followed by a zero byte not counted in
  `size`, so a JSON result reads as a C string. A call overwrites the buffer
  without freeing what it held, so a buffer is freed before it is reused.
- **No exception crosses the ABI**: every exported body runs inside a
  catch-all that turns any failure into a status and an error object. An
  out-of-memory condition is `PDFKIT_E_INTERNAL`; when even the error object
  cannot be allocated, the status alone is returned and the buffer stays empty.
- **Calling convention**: the platform's C default (`cdecl` on Windows x64,
  where there is one convention). From .NET: `[DllImport("pdfkit")]` with
  `CallingConvention.Cdecl`; from Python: `ctypes.CDLL`.
- **Linking**: `pdfkit.dll` imports only operating-system DLLs (KERNEL32 and the
  Universal C Runtime API sets). C and C++ callers link `pdfkit.lib` (the import
  library) and include `pdfkit/pdfkit.h` without defining anything.

## 2. Status codes

| Value | Status | Meaning | Error codes it carries |
|---|---|---|---|
| 0 | `PDFKIT_OK` | the call succeeded; the buffer holds the result | none |
| 1 | `PDFKIT_E_INVALID_ARGUMENT` | a null handle or required argument, or malformed JSON input | `invalid_argument` |
| 2 | `PDFKIT_E_REFUSED` | the input document is refused | `not_pdf`, `no_page`, `password_required`, `unsupported_security_handler`, `limit_exceeded`, `page_ceiling`, and `undecodable_image` for one image that does not decode (`docs/DESIGN.md` §19) |
| 3 | `PDFKIT_E_SELF_CHECK` | the self-check found a redacted string in the output; no output is returned | `self_check` (`docs/DESIGN.md` §18) |
| 4 | `PDFKIT_E_NOT_IMPLEMENTED` | the catalogue function is not part of this build | `not_implemented` |
| 5 | `PDFKIT_E_INTERNAL` | a defect in pdfkit | `internal` |

The values never change. A new refusal code is added to `docs/DESIGN.md` §19
and rides under `PDFKIT_E_REFUSED`; a new status value is an ABI addition
(§5). Warnings (`repaired`, `damaged_stream`, `unreadable_text`, and the rest of
`docs/DESIGN.md` §19) never fail a call: they are listed in the result's
`warnings` array, each an object with a `code` and the fields the design names.

## 3. The JSON error object

Every failure writes one object with four string fields, each non-empty: the
library cannot build an error without them.

```json
{
  "code": "invalid_argument",
  "failed": "pdfkit_text options_json: the JSON input is malformed at byte 13: found ']' where a value was expected",
  "expected": "a JSON value: an object, an array, a string, a number, true, false or null",
  "fix": "correct the input at byte 13; it must be strict JSON (RFC 8259): double-quoted strings, no comments, no trailing commas, no leading zeros, UTF-8 text"
}
```

| Field | Holds |
|---|---|
| `code` | the stable code of §2, for programs |
| `failed` | what failed, naming the symbol, the parameter, the object, the page or the byte offset |
| `expected` | what the library expected there |
| `fix` | what the caller can do about it |

A function not part of this build answers with `code` `not_implemented`, a
`failed` naming the symbol, its catalogue number and its function, and an
`expected` naming the build file that delivers it:

```json
{
  "code": "not_implemented",
  "failed": "pdfkit_text (catalogue function 2, Extract text) is not implemented in this build of pdfkit",
  "expected": "a pdfkit build that implements pdfkit_text, which build-5.md delivers",
  "fix": "use a pdfkit build whose pdfkit_version output lists pdfkit_text with \"implemented\": true"
}
```

## 4. The symbols

Each function checks its arguments first (a null `out`, handle or required
string, malformed JSON), then does its work. An optional JSON argument may be
`NULL`, which means `{}`.

### 4.1 Lifecycle

| Symbol | Signature | Behaviour |
|---|---|---|
| `pdfkit_abi_version` | `uint32_t pdfkit_abi_version(void)` | the ABI version, 1 |
| `pdfkit_version` | `pdfkit_status pdfkit_version(pdfkit_buffer* out)` | the version object below |
| `pdfkit_buffer_free` | `void pdfkit_buffer_free(pdfkit_buffer* buffer)` | frees and zeroes the buffer; a zeroed buffer or `NULL` is a no-op |
| `pdfkit_close` | `void pdfkit_close(pdfkit_document* doc)` | destroys the document; `NULL` is a no-op |
| `pdfkit_edit_new` | `pdfkit_status pdfkit_edit_new(pdfkit_document* doc, pdfkit_edit** out_edit, pdfkit_buffer* out)` | creates an empty edit plan over `doc`; `out` receives `{}`; `PDFKIT_E_INVALID_ARGUMENT` for a null `doc` or `out_edit` |
| `pdfkit_edit_free` | `void pdfkit_edit_free(pdfkit_edit* edit)` | destroys the edit plan; `NULL` is a no-op |

The version object:

```json
{
  "version": "0.1.0",
  "abi": 1,
  "build": "gcc 16.1.0, Release",
  "catalogue": [
    {"number": "1.1", "function": "Open", "symbol": "pdfkit_open", "verb": "inspect",
     "build_file": "build-3.md", "implemented": false,
     "error": {"code": "not_implemented", "failed": "...", "expected": "...", "fix": "..."}}
  ]
}
```

`catalogue` holds one entry per row of `CLAUDE.md` §2 in its order; `error` is
present only when `implemented` is false and is exactly the object the
function returns, so a caller (the CLI among them) can report a missing
function before reading any input. `build` names the compiler and the build
type and never a time, so two builds of one tree report the same text.

### 4.2 Catalogue functions

| # | Signature | Required arguments | Output on success | Delivered by |
|---|---|---|---|---|
| 1.1 | `pdfkit_open(const uint8_t* bytes, size_t size, const char* options_json, pdfkit_document** out_doc, pdfkit_buffer* out)` | `out`, `out_doc`; `bytes` unless `size` is 0 | `*out_doc` the handle; `out` the open report | `build-3.md` |
| 1.2 | `pdfkit_inspect(pdfkit_document* doc, const char* options_json, pdfkit_buffer* out)` | `doc`, `out` | the inspect JSON | `build-3.md` |
| 2 | `pdfkit_text(pdfkit_document* doc, const char* options_json, pdfkit_buffer* out)` | `doc`, `out` | the text JSON | `build-5.md` |
| 3.1 | `pdfkit_images(pdfkit_document* doc, const char* options_json, pdfkit_buffer* out)` | `doc`, `out` | the image list JSON | `build-6.md` |
| 3.2 | `pdfkit_image_data(pdfkit_document* doc, const char* identity, const char* options_json, pdfkit_buffer* out)` | `doc`, `identity`, `out` | PNG or JPEG bytes (not JSON) | `build-6.md` |
| 4 | `pdfkit_metadata(pdfkit_document* doc, const char* options_json, pdfkit_buffer* out)` | `doc`, `out` | the metadata JSON | `build-10.md` |
| 5 | `pdfkit_redact_text(pdfkit_edit* edit, const char* matches_json, pdfkit_buffer* out)` | `edit`, `matches_json`, `out` | the per-match JSON | `build-8.md` |
| 6 | `pdfkit_redact_images(pdfkit_edit* edit, const char* treatments_json, pdfkit_buffer* out)` | `edit`, `treatments_json`, `out` | the per-treatment JSON | `build-9.md` |
| 7 | `pdfkit_set_metadata(pdfkit_edit* edit, const char* fields_json, pdfkit_buffer* out)` | `edit`, `fields_json`, `out` | the fields as they will be written | `build-10.md` |
| 8 | `pdfkit_channels(pdfkit_document* doc, const char* options_json, pdfkit_buffer* out)` | `doc`, `out` | the channel report JSON | `build-7.md` |
| 9 | `pdfkit_select_pages(pdfkit_edit* edit, const char* pages_json, pdfkit_buffer* out)` | `edit`, `pages_json`, `out` | `{"pages": [...]}`, the pages recorded | `build-7.md` |
| 10.1 | `pdfkit_write(pdfkit_edit* edit, const char* options_json, pdfkit_buffer* out_pdf, pdfkit_buffer* out)` | `edit`, `out_pdf`, `out` | `out_pdf` the PDF bytes; `out` the write report | `build-7.md`, self-check `build-8.md` |
| 10.2 | `pdfkit_check(const uint8_t* bytes, size_t size, const char* strings_json, pdfkit_buffer* out)` | `strings_json`, `out`; `bytes` unless `size` is 0 | the surviving occurrences JSON | `build-8.md` |

Every function returns `PDFKIT_E_INVALID_ARGUMENT` for a missing required
argument or malformed JSON, and `PDFKIT_E_NOT_IMPLEMENTED` until the build file
in the last column lands; the build file that implements a function adds its
JSON shapes and its failure codes to a section of this chapter. `pdfkit_write`
is the one function with two output buffers: the PDF bytes are not JSON, and
the write report travels beside them; on failure `out_pdf` stays empty and
`out` holds the error.

### 4.3 pdfkit_open (catalogue 1.1)

`pdfkit_open` copies the bytes, finds the `%PDF-` header within the first
1,024 bytes, reads the cross-reference table along the trailer chain or
rebuilds it by scanning when that path disappoints, decrypts with the empty
user password, parses every object once (so every recovery the file needs is
named at open) and walks the page tree (`docs/DESIGN.md` chapters 1 to 4).

The options are the limits of `docs/DESIGN.md` §3.2, each a positive integer;
any other key, or a value that is not a positive integer, is
`invalid_argument`:

| Option | Default | Bounds |
|---|---|---|
| `stream_bytes` | 536870912 (512 MiB) | the decoded bytes of one stream |
| `document_bytes` | 2147483648 (2 GiB) | the decoded bytes of the whole document |
| `objects` | 8388607 | the objects the cross-reference table or the scan names |
| `page_ceiling` | 10000 | the pages of the document |
| `operators_per_page` | 50000000 | the content operators of one page |

On success `*out_doc` receives the handle and `out` the open report:

```json
{
  "version": "1.7",
  "page_count": 3,
  "encryption": {"filter": "Standard", "version": 4, "revision": 4, "method": "AESV2",
                 "key_bits": 128, "encrypt_metadata": true},
  "repairs": ["the file was reconstructed by scanning it, because startxref 1234 does not land on xref or a cross-reference stream"],
  "warnings": [{"code": "repaired", "message": "...", "page": null, "reasons": ["..."]},
               {"code": "damaged_stream", "message": "...", "page": 2}]
}
```

| Key | Holds |
|---|---|
| `version` | the header's version, raised by a later catalog `/Version`; empty when no header was found |
| `page_count` | the pages in tree order |
| `encryption` | `null` for an unencrypted file; otherwise the standard handler's version (`/V`), revision (`/R`), method (`RC4`, `AESV2`, `AESV3` or `Identity`), key length in bits and whether the metadata is encrypted |
| `repairs` | every recovery the file needed, each named once, in the order met; at most 100, the last line then counting the rest; empty for a file read as written |
| `warnings` | the warnings of `docs/DESIGN.md` chapter 19 met while opening, each with `code`, `message` and `page` (`null` when it concerns no page); when `repairs` is not empty the first is `repaired`, which also carries the `reasons` |

Failures, each with the error object of §3: `invalid_argument` (a null `out` or
`out_doc`, `bytes` null with a non-zero `size`, malformed or unknown options),
and the refusals `not_pdf`, `no_page`, `password_required`,
`unsupported_security_handler`, `limit_exceeded` and `page_ceiling` of
`docs/DESIGN.md` chapter 19 under `PDFKIT_E_REFUSED`. On failure `*out_doc` is
`NULL`.

### 4.4 pdfkit_inspect (catalogue 1.2)

`pdfkit_inspect` reports what open read; it takes no options (`NULL` or `{}`;
anything else is `invalid_argument`) and never fails on a valid handle:

```json
{
  "version": "1.7",
  "page_count": 2,
  "pages": [{"page": 1, "media_box": [0, 0, 612, 792], "crop_box": [10, 20, 300, 400], "rotate": 90}],
  "encryption": null,
  "repairs": [],
  "warnings": []
}
```

`pages` holds one object per page in tree order: its number from 1, the media
box and the crop box as `[left, bottom, right, top]` in points (normalised, the
crop box clipped to the media box, a missing media box being US Letter), and
the rotation (0, 90, 180 or 270). `encryption`, `repairs` and `warnings` are
those of the open report at the time of the call. Keys are written in the order
shown, so two calls give the same bytes.

### 4.5 pdfkit_channels (catalogue 8)

`pdfkit_channels` reports, per row of the channel table (`docs/DESIGN.md`
§15, `src/writer/channel_table.cpp`), whether the channel is present in the
document and how many occurrences it holds; every `pdfkit_write` strips the
same rows. It takes no options (`NULL` or `{}`; anything else is
`invalid_argument`) and never fails on a valid handle.

```json
{
  "channels": [{"id": "ch_revisions", "name": "earlier revisions (incremental updates)", "present": true, "count": 1}],
  "present": ["ch_revisions"],
  "pending_redactions": 0,
  "warnings": [{"code": "pending_redactions", "message": "...", "page": 2}]
}
```

`channels` holds the 23 rows in the table's order, each with its `id` (the
dataset's taxonomy tag), its `name`, `present` and `count`; `present` lists the
ids whose count is above zero. The counts: `ch_revisions` the sections the
`startxref` chain adds after the first (a linearized file's first-page section
discounted); `ch_unreferenced` the objects of the table no reference from the
trailer reaches (cross-reference and object streams aside); `ch_thumbnail`,
`ch_web_capture` the pages carrying the key (plus one for `/SpiderInfo`);
`ch_pieceinfo`, `ch_last_modified`, `ch_object_metadata` the dictionaries
carrying the key (the catalog's `/Metadata` aside); `ch_javascript` the
JavaScript actions; `ch_embedded_file` the `/EF` and `/AF` entries;
`ch_annotation` the annotations of every page; `ch_acroform` the top-level
fields plus one for `/XFA`; `ch_outline` the outline items; `ch_named_dest` the
`/Dests` entries and the leaves of every `/Names` tree; `ch_struct_tree` the
structure elements; `ch_optional_content` the groups of `/OCGs`;
`ch_page_labels` the label ranges; `ch_xmp_history` 1 when the catalog's
packet holds `xmpMM:History`; `ch_threads` the threads; the catalog keys
(`ch_portfolio`, `ch_perms_legal`, `ch_uri_base`, `ch_search_index`) 1 per key
present; `ch_encrypt` 1 for an encrypted input; `ch_xobject_private` the keys
outside the allow-lists of `docs/DESIGN.md` §15 over every form and image
XObject. A structure that is present
but empty counts 1. `pending_redactions` counts the `/Redact` annotations
never applied, and each page holding one has a `pending_redactions` warning.

### 4.6 pdfkit_select_pages (catalogue 9)

`pdfkit_select_pages` records on an edit plan the pages the output keeps, in
the order given; `pdfkit_write` applies it. The input is
`{"pages": [3, 1, 2]}`, 1-based page numbers of the document as pdfkit reads
it. It is refused with `invalid_argument` (and the three message parts naming
the page and the document's page count) when the list is empty, names a page
twice or a page outside 1 to the page count, holds anything but whole numbers,
or the object holds another key. A later call replaces the list. On success
`out` holds `{"pages": [3, 1, 2]}`.

### 4.7 pdfkit_write (catalogue 10.1)

`pdfkit_write` applies the edit plan in the order of `CLAUDE.md` §3.2 and
returns a fresh, unencrypted file of the reachable objects
(`docs/DESIGN.md` §17) with every channel of §15 stripped and, when pages were
selected, only those pages (§16). It takes no options (`NULL` or `{}`). On
success `out_pdf` holds the PDF bytes and `out` the write report; on failure
`out_pdf` stays empty and `out` holds the error (a refusal met while decoding
a content stream for pruning, such as `limit_exceeded`, is the document's
refusal under `PDFKIT_E_REFUSED`). Writing one plan twice gives the same
bytes.

```json
{
  "page_count": 2,
  "pages": [3, 1],
  "bytes": 18273,
  "sha256": "<hex of the output>",
  "objects": 41,
  "channels": [{"id": "ch_revisions", "stripped": 1}],
  "pending_redactions": 0,
  "missing_references": 0,
  "unpruned_resources": 0,
  "rectangles": [],
  "images": [],
  "self_check": {"status": "not_run", "reason": "no text redaction in the edit plan"},
  "warnings": []
}
```

With a text redaction in the plan, `rectangles` and `self_check` read:

```json
  "rectangles": [{"page": 1, "source_page": 3, "placeholders": 1,
                  "boxes": [[70.17, 697.9, 129.6, 711.2], [70.17, 696.4, 131.2, 712.1]]}],
  "self_check": {"status": "passed", "strings": 2},
```

| Key | Holds |
|---|---|
| `page_count`, `pages` | the output's pages and, in output order, the source page each one is |
| `bytes`, `sha256` | the output's size and SHA-256 |
| `objects` | the indirect objects written, the cross-reference stream aside |
| `channels` | per row of the table, in its order, the occurrences the source held, which the output no longer carries (`ch_optional_content` is kept in its minimal form) |
| `pending_redactions` | the source's `/Redact` annotations never applied; the output drops them with every annotation |
| `missing_references` | the distinct object numbers referenced but missing, written as `null` |
| `unpruned_resources` | the resource dictionaries kept whole because their content stream did not read whole |
| `rectangles` | per output page a text redaction or an image treatment changed (`page`, and `source_page` it was): the `boxes` it changed (`[left, bottom, right, top]` in default user space, rounded to 0.01: each removed run per reading line with the glyphs drawn after it on that line, each placeholder drawn, widened by a sixth of the glyph size, and per draw of a treated image the page bound of each treated pixel rectangle, or the draw's box when the image is removed) and the `placeholders` drawn there; empty when nothing was redacted |
| `images` | per image identity the plan treats, in identity order: `identity`, `treatment` (`box`, `blur`, `remove`, or `region` for region boxes alone), `objects` (the image objects replaced or emptied), `inline_draws` (the inline draws rewritten or removed), `draws_removed` (the `Do` and `BI` operations removed), `sha256` (per replaced object, and once per rewritten inline image, the SHA-256 of the new encoded data as stored: the stream's bytes, or the inline data from after `ID` to its closing `>`); empty without a treatment |
| `self_check` | `{"status": "not_run", "reason": ...}` when the plan holds no text match and no rewritten metadata field; `{"status": "passed", "strings": n}` when the output was re-read and none of the n redacted strings (the text matches' strings, and the original values of the metadata fields rewritten) survives (`docs/DESIGN.md` §18); a survivor fails the whole call with `PDFKIT_E_SELF_CHECK` and the error object names every survivor's string, surface and page, and no PDF is returned |
| `warnings` | the `pending_redactions` warnings of the source, and `xmp_unreadable` when the catalog's XMP packet did not decode or scan and was dropped |

### 4.8 pdfkit_image_data (catalogue 3.2)

`pdfkit_image_data(doc, identity, options_json, out)` writes one image's file
bytes into `out` (`docs/DESIGN.md` §9.5); the bytes are not JSON. `identity`
is an image identity (`img:` and 16 lower-case hexadecimal digits, `CLAUDE.md`
§4) of an image XObject the document's pages reach through their resources,
their forms' resources, or the resources of a tiling pattern or soft-mask
group, or of an inline image a page draws (found by reading the pages' content
when no XObject has the identity). The options:

| Key | Value | Default | Effect |
|---|---|---|---|
| `mask` | `true` or `false` | `false` | the image's mask instead of the image: the `/SMask` (grey, 8 or 16 bits), a `/Mask` stencil or colour key (1-bit grey, white where the image shows), or the JPEG 2000 alpha of `/SMaskInData`, at the mask's own size |
| `decode_jpeg` | `true` or `false` | `false` | a PNG of a `DCTDecode` image's decoded pixels instead of its JPEG bytes |

On success `out` holds:

- for a `DCTDecode` image (the codec last in its chain), the JPEG bytes as
  stored, after any non-image filter before the codec;
- for every other image, a PNG: grey, grey with alpha, RGB or RGB with
  alpha, at 1 bit (a bitonal grey image), 8 bits, or 16 bits when the source
  is 16-bit; an `/ImageMask` stencil is a 1-bit grey PNG, black where it
  paints, its white made transparent by `tRNS`. CMYK, Lab, Indexed,
  Separation and DeviceN images are converted to RGB (`docs/DESIGN.md` §9.4).

The PNG bytes depend on the pixels alone, so two calls give the same bytes.

| Failure | Status | `code` |
|---|---|---|
| a null `doc` or `out`, a null or empty `identity`, malformed `options_json`, an unknown option, a non-boolean option | `PDFKIT_E_INVALID_ARGUMENT` | `invalid_argument` |
| an identity that names no image of the document | `PDFKIT_E_INVALID_ARGUMENT` | `invalid_argument` |
| `mask` on an image without a mask | `PDFKIT_E_INVALID_ARGUMENT` | `invalid_argument` |
| an image whose encoding does not decode (the message names the identity, the encoding, the object and the fault) | `PDFKIT_E_REFUSED` | `undecodable_image` |
| decoding past a limit of the open options | `PDFKIT_E_REFUSED` | `limit_exceeded` |

The first image call on a handle lists the document's images once and keeps
the list with the handle; every call charges the bytes it decodes to the
document's decoding budget (`docs/DESIGN.md` §3.2).

### 4.9 pdfkit_text (catalogue 2)

`pdfkit_text` reads each page through the content interpreter, the decoding
ladder's `/ActualText` rung, the hidden-text classes and the layout
(`docs/DESIGN.md` chapters 6 to 8) and returns the pages in the order asked.
Options, each optional; any other key, or a value of another type, is
`invalid_argument`:

| Option | Default | Meaning |
|---|---|---|
| `pages` | every page | an array of page numbers from 1, each at most once; a number outside the document is `invalid_argument`, checked before any page is read |
| `blocks` | `true` | write `blocks` and `hidden_blocks` (the page's `text` is always written) |
| `glyphs` | `false` | write `glyphs` (every glyph record) and `spans` (the marked-content spans carrying `/ActualText`, `/Alt` or `/E`) |
| `reserialisation` | `false` | write `reserialisation`, the check that every content stream walk writes back to its own bytes |

```json
{"pages": [{
  "page": 1,
  "text": "Hello World\n\nHid",
  "structure_order": false,
  "blocks": [{"box": [72, 697.93, 123.67, 707.18], "glyphs": [0, 11], "lines": [
    {"box": [72, 697.93, 123.67, 707.18], "glyphs": [0, 11], "direction": "ltr", "angle": 0,
     "words": [{"text": "Hello", "box": [72, 697.93, 94.78, 707.18], "glyphs": [0, 5],
                "hyphenated": false, "hidden": []}]}]}],
  "hidden_blocks": [],
  "unreadable": {"count": 0, "fonts": []},
  "hidden": {"count": 0, "classes": {"render_invisible": 0, "white_fill": 0, "clipped": 0,
             "off_media": 0, "off_crop": 0, "optional_content_off": 0, "duplicate": 0, "covered": 0}},
  "rungs": {"to_unicode": 0, "glyph_name": 11, "font_program": 0, "actual_text": 0, "unreadable": 0},
  "warnings": [{"code": "hidden_text", "message": "...", "page": 1}],
  "reserialisation": {"streams": 1, "identical": 1, "damaged": 0, "failed": 0, "mismatches": []},
  "spans": [{"tag": "Span", "actual_text": "fi", "alt": null, "expansion": null, "walk": 0, "operator": 4}],
  "glyphs": [{"draw": 0, "stream": {"walk": 0, "form": null, "form_path": []}, "operator": 3,
              "operand": 0, "bytes": [31, 32], "hex": false,
              "font": {"object": 3, "name": "Helvetica"}, "code": 72, "code_length": 1,
              "text": "H", "rung": "glyph_name", "decomposed": false, "width": 722,
              "adjustment": 0, "advance": 7.22, "vertical": false,
              "text_matrix": [1, 0, 0, 1, 72, 700], "ctm": [1, 0, 0, 1, 0, 0],
              "char_spacing": 0, "word_spacing": 0, "horizontal_scaling": 100, "leading": 0,
              "rise": 0, "font_size": 10, "render_mode": 0, "origin": [72, 700],
              "advance_end": [79.22, 700], "box": [72, 697.93, 79.22, 707.18], "size": 10,
              "angle": 0, "fill": {"space": "DeviceGray", "family": "DeviceGray", "components": [0]},
              "marked_content": null, "actual_text": null,
              "optional_content": {"groups": [], "off": false}, "clip": null, "hidden": [],
              "duplicate_of": null, "duplicates": []}]
}],
 "fingerprint": "83ce7cec117fa30dbe1a3a3dc1d454be02cf1c463c8c69dcc8d69ce41f19c89f"}
```

| Key | Holds |
|---|---|
| `text` | the page's text in reading order: words joined by a space, lines by a line feed, blocks by an empty line; the hidden blocks follow the visible ones; a word whose only hidden class is `duplicate` is a copy of a word already read and is not read again |
| `structure_order` | whether the tagged structure ordered the visible blocks (`/StructTreeRoot` present and every MCID it orders on the page drawn by the page's content) |
| `blocks`, `hidden_blocks` | the visible and the hidden blocks in reading order, each with `lines`, each with `words`; every one carries its `box` (`[left, bottom, right, top]` in default user space, points, rounded to 0.01) and `glyphs`, the half-open range of positions it covers in the page's glyph list; a line carries its `direction` (`ltr`, `rtl`, `ttb`) and `angle` (degrees, the direction its text advances); a word its `hidden` classes and `hyphenated`, true when its line-ending hyphen joins it to the first word of the next line (on the next page too, when both pages are asked together) |
| `unreadable` | the codes no rung of the decoding ladder read, in all and per font name |
| `hidden` | the glyphs of a hidden class, in all and per class (a glyph of several classes counts once in `count` and once per class) |
| `rungs` | the glyphs per decoding rung |
| `warnings` | the page's warnings of `docs/DESIGN.md` chapter 19: those the document met while the page was read (`damaged_stream`, `resource_missing`), then `unreadable_text`, `hidden_text` and `font_not_embedded` |
| `reserialisation` | the walks of the page's content (its `/Contents` and each form draw), those that write back to their own bytes, those read only up to a fault, the undamaged walks that do not (`failed`, always 0 for a sound reader), and the first mismatch of each failing walk |
| `glyphs` | the glyph list in reading order (the visible blocks' glyphs, then the hidden blocks'), each record with every field of `CLAUDE.md` §4: `draw` (its place in draw order), `stream` (the walk: its index, the form object or `null` for the page, the form path), `operator` and `operand` (the operator's index in its walk and the operand holding the code, for `TJ` the index inside the array), `bytes` (the half-open byte range of the code as written in the walk's bytes) and `hex`, `font`, `code` and `code_length`, `text` and `rung`, `decomposed` (a ligature's letters), `width` (thousandths of text space), `adjustment` (the `TJ` number before it), `advance` (text space, `Tc` and `Tw` included), `vertical`, the text state (`text_matrix` before the glyph, `ctm`, `char_spacing`, `word_spacing`, `horizontal_scaling`, `leading`, `rise`, `font_size`, `render_mode`), the device geometry (`origin`, `advance_end`, `box`, `size`, `angle`), `fill`, `marked_content` (innermost tag and MCID), `actual_text` (the span and whether the span's text went to an earlier glyph), `optional_content`, `clip` (`null` when nothing clips), `hidden`, `duplicate_of` and `duplicates` (draw indices) |
| `spans` | per marked-content span with replacement text, in the order met: its tag, `/ActualText`, `/Alt` and `/E`, and the walk and operator of its `BDC` |
| `fingerprint` | after the pages: the document fingerprint, the SHA-256 (lower-case hexadecimal) of the file from its `%PDF-` header on, which a glyph range given to `pdfkit_redact_text` carries back so a range of another document is refused |

Every key is written in the order shown and every geometric number rounded to
0.01 pt, so two calls give the same bytes. Failures: `invalid_argument` for a
null handle or `out`, malformed or unknown options, or a page number outside
the document; a refusal of the document itself met while a page is decoded
(`limit_exceeded`) under `PDFKIT_E_REFUSED`.

### 4.10 pdfkit_images (catalogue 3.1)

`pdfkit_images(doc, options_json, out)` lists the images the asked pages draw
(`docs/DESIGN.md` §9.1, §9.2). The draws come from the content interpreter,
through forms, in each page's draw order; they are grouped by image identity
(`CLAUDE.md` §4), so one picture drawn on several pages, or stored twice, or
once as an XObject and once inline, is one entry with several occurrences. An
image a page reaches only inside a tiling pattern or a soft-mask group it sets
(which the interpreter does not walk) is listed with that page in
`pattern_pages`, no occurrence, and the warning `image_in_pattern`. Each image
is decoded once to say whether it decodes; one that does not carries the
warning `undecodable_image`, and `pdfkit_image_data` refuses it. The handle
keeps what it read, so a later call reads only pages not read yet.

The only option is `pages`, an array of page numbers from 1, each at most
once (default: every page); an image is listed when it is drawn, or reached
inside a pattern, on one of them, with only those pages' occurrences.

```json
{
  "pages": [1, 2],
  "count": 1,
  "images": [
    {"identity": "img:0123456789abcdef", "source": "xobject", "object": 12,
     "encoding": "DCTDecode", "filters": ["DCTDecode"], "width": 640, "height": 480,
     "bits_per_component": 8, "colour_space": "DeviceRGB", "components": 3,
     "image_mask": false, "mask": "smask", "interpolate": false,
     "decodable": true, "warnings": [],
     "occurrences": [{"page": 1, "ordinal": 0, "box": [72.00, 500.00, 300.00, 671.00],
                      "form_path": [], "optional_content_off": false}],
     "pattern_pages": []}
  ],
  "warnings": [{"code": "undecodable_image", "message": "...", "page": 2}]
}
```

| Key | Holds |
|---|---|
| `pages`, `count` | the pages read and the number of images listed |
| `identity` | the key `pdfkit_image_data` and `pdfkit_redact_images` take |
| `source`, `object` | `xobject` with its object number (`null` for a direct stream), or `inline` with `null` |
| `encoding`, `filters` | the last filter by its full name (`none` without one) and the chain in order |
| `width`, `height`, `bits_per_component` | as the dictionary states them, `null` when absent |
| `colour_space`, `components` | the colour space's family (`DeviceRGB`, `ICCBased`, `Indexed`, ..., `invalid` for one that does not parse) and its component count; `null` for a stencil or an image that states none |
| `image_mask`, `mask`, `interpolate` | `/ImageMask`; the mask kind (`none`, `smask`, `stencil`, `color_key`, `smask_in_data`); `/Interpolate` |
| `decodable`, `warnings` | whether the image decodes, and its warning codes (`undecodable_image`, `image_in_pattern`) |
| `occurrences` | one per draw on the asked pages: `page`, `ordinal` (the draw's place among the page's image draws, from 0, forms included), `box` (the unit square's device bound, `[left, bottom, right, top]` rounded to 0.01 pt), `form_path` (the form XObjects drawn through, outermost first), `optional_content_off` (the default configuration hides the draw) |
| `pattern_pages` | the asked pages on which it is reached only inside a pattern or soft mask |
| `warnings` (top level) | one object per image warning, with `code`, `message` and `page` (the first page concerned) |

Images are ordered by their first draw on the asked pages (page, then
ordinal), an image reached only inside a pattern after that page's draws, then
by identity. A page out of range, a page listed twice, a non-array `pages` or
an unknown option is `invalid_argument`; a limit met while reading a content
stream is the document's refusal (`limit_exceeded`); a limit met while
probing one image is that image's `undecodable_image` warning.

### 4.11 pdfkit_redact_text (catalogue 5)

`pdfkit_redact_text(edit, matches_json, out)` adds text matches to the edit
plan (`docs/DESIGN.md` §11 to §13); the text is removed when the plan is
written. `matches_json` is `{"matches": [...]}`, each match one of:

| Form | Keys |
|---|---|
| search | `search` (the string, UTF-8), optional `folding` (`case`, `diacritics`, `whitespace`, `hyphenation`, each `true` by default: simple case folding, marks stripped after canonical decomposition, any run of white space or an inferred word gap as one space, a joined hyphenation read without its hyphen), optional `placeholder` |
| glyph range | `glyphs` (`{"page": n, "first": i, "last": j}`, a half-open range of positions in the page's glyph list of `pdfkit_text`), `fingerprint` (the one `pdfkit_text` reported), optional `placeholder` |

The placeholder defaults to `[REDACTED]`, holds only the characters `[`, `]`,
`_`, `0` to `9` and `A` to `Z`, and may be empty, which removes the text and
draws nothing. A search runs over every page's text as `pdfkit_text` writes it,
hidden blocks included, and over the other orders a reader reads the page in
(the glyphs as drawn, and each baseline of the page read from either end,
`docs/DESIGN.md` §11), and matches inside words; an occurrence found only in
another order is reported once per stretch of the glyph list it covers, the
first drawing the placeholder. An Arabic presentation form matches its
letters under every folding. Every match is checked
before any is recorded, so a refused call leaves the plan as it was. Calls add
up; occurrences that overlap across matches follow the overlap rule of
`docs/DESIGN.md` §11 when the plan is written.

```json
{"fingerprint": "83ce7c...",
 "matches": [{"match": 0, "kind": "search", "search": "Jane Doe", "placeholder": "[REDACTED]",
              "count": 1,
              "occurrences": [{"page": 1, "glyphs": [10, 18], "text": "Jane Doe",
                               "rectangles": [[70.17, 696.26, 109.8, 708.85]],
                               "placeholder_drawn": true, "fitting": "scaled", "scale": 0.762,
                               "codes": ["4a616e6520446f65"]}]}]}
```

| Key | Holds |
|---|---|
| `fingerprint` | the document fingerprint |
| `match` | the match's index in this call; `kind` `search` (with `search`) or `glyphs` |
| `count`, `occurrences` | per occurrence found (zero is an answer, not a failure): `page`, `glyphs` (its range in the page's glyph list), `text` (as the extraction reads it), `rectangles` (the device boxes it changes), `placeholder_drawn`, `fitting` (`natural`, `scaled` or `overflow`, `null` when nothing is drawn) and `scale` (the share of the run's horizontal scaling the placeholder is drawn at), `codes` (the removed codes per font run, hexadecimal) |

| Failure | Status | `code` |
|---|---|---|
| a null `edit` or `out`, malformed JSON, no `matches` array, an unknown key, a match with both or neither of `search` and `glyphs`, a non-boolean folding | `PDFKIT_E_INVALID_ARGUMENT` | `invalid_argument` |
| a placeholder with a character outside the alphabet (the message names it) | `PDFKIT_E_INVALID_ARGUMENT` | `invalid_argument` |
| a glyph range whose fingerprint is not this document's, a page outside the document, a range empty or past the page's glyphs | `PDFKIT_E_INVALID_ARGUMENT` | `invalid_argument` |
| a limit met while a page is read | `PDFKIT_E_REFUSED` | `limit_exceeded` |

### 4.12 pdfkit_check (catalogue 10.2)

`pdfkit_check(bytes, size, strings_json, out)` runs the search of the
self-check (`docs/DESIGN.md` §18) over any PDF bytes, opened with the default
limits. `strings_json` is `{"strings": [{"text": "...", "folding": {...}}]}`,
the folding as in §4.11. Finding a string is the answer:

```json
{"strings": 2, "pages": 1, "count": 1,
 "survivors": [{"string": "zebulon", "page": 1, "surface": "text", "object": null}]}
```

`surface` is `text` (a page's text, hidden text included), `actual_text`,
`alt`, `expansion`, `info`, `xmp`, `string` (a string object, `object` naming
it) or `stream` (the decoded bytes of a stream or of a content stream's string
operands, `object` naming it); `page` is null for a surface outside the pages.
Failures: `invalid_argument` for a null `out`, malformed or unknown keys, a
string without `text`; the open's refusal (`not_pdf`, `password_required` and
the rest) under `PDFKIT_E_REFUSED` when the bytes do not open.

### 4.13 pdfkit_metadata (catalogue 4)

`pdfkit_metadata(doc, options_json, out)` reads the Info dictionary and the
catalog's XMP packet side by side (`docs/DESIGN.md` §10). It takes no option
(`NULL` or `{}`; any key is `invalid_argument`) and never fails on a valid
handle: a packet that does not scan is reported, with the warning
`xmp_unreadable`, and its fields read as absent.

```json
{"info": true, "xmp": "read", "xmp_fault": null, "agree": false, "utf16le": 0,
 "xmp_namespaces": ["http://ns.adobe.com/pdf/1.3/", "http://purl.org/dc/elements/1.1/"],
 "xmp_history": false,
 "fields": [
  {"field": "Title", "info": "Report", "info_encoding": "pdf_doc", "xmp": "Report",
   "xmp_property": "dc:title",
   "xmp_values": [{"property": "dc:title", "value": "Report", "items": ["Report"]}],
   "agree": true},
  {"field": "CreationDate", "info": "D:20200102030405Z", "info_encoding": "pdf_doc",
   "xmp": "2020-01-02T03:04:05Z", "xmp_property": "xmp:CreateDate",
   "xmp_values": [{"property": "xmp:CreateDate", "value": "2020-01-02T03:04:05Z", "items": []}],
   "info_iso": "2020-01-02T03:04:05Z", "xmp_iso": "2020-01-02T03:04:05Z", "agree": true}
 ],
 "warnings": []}
```

| Key | Holds |
|---|---|
| `info` | whether the trailer names an Info dictionary |
| `xmp`, `xmp_fault` | `absent`, `read` or `unreadable`, and for `unreadable` why the packet did not scan |
| `agree` | false when any field's two values disagree |
| `utf16le` | the Info strings written UTF-16LE with a mark, which the reader tolerates |
| `xmp_namespaces`, `xmp_history` | the namespaces of the packet's properties, sorted, and whether it holds an `xmpMM:History` |
| `fields` | one row per field of the field list, always eight, in the order `Title`, `Author`, `Subject`, `Keywords`, `Creator`, `Producer`, `CreationDate`, `ModDate` |
| `info`, `info_encoding` | the Info value as UTF-8 (`null` when absent) and the form its string was written in: `pdf_doc`, `utf16be`, `utf16le` or `utf8` |
| `xmp`, `xmp_property` | the first paired XMP value present and the property it came from, whatever prefix the packet writes reported under the usual one (`dc:title`, `dc:creator`, `dc:description`, `pdf:Keywords` then `dc:subject`, `xmp:CreatorTool`, `pdf:Producer`, `xmp:CreateDate`, `xmp:ModifyDate`) |
| `xmp_values` | every paired property present: its value (an `rdf:Alt`'s `x-default` item, an `rdf:Seq`'s or `rdf:Bag`'s items joined with `; `) and an array's items |
| `info_iso`, `xmp_iso` | for the two dates only: each value in ISO 8601 when it parses, else `null` |
| `agree` (per field) | whether the Info value and the first XMP value say the same (dates compared in ISO 8601, author and keyword lists item by item); `null` when either is absent |

### 4.14 pdfkit_set_metadata (catalogue 7)

`pdfkit_set_metadata(edit, fields_json, out)` records field values on the
edit plan; `pdfkit_write` writes them at its metadata stage
(`docs/DESIGN.md` §10). `fields_json` is one object keyed by the field
names of §4.13, each a string (the new value; a date may be given as a PDF
date or in ISO 8601 and is written in each place's form) or `null` (the field
is removed from both places). A later call's value of a field replaces an
earlier one. The answer lists every field the plan holds as it will be
written:

```json
{"fields": [
  {"field": "Title", "removed": false, "info": "New title", "info_encoding": "pdf_doc",
   "xmp": "New title", "xmp_properties": ["dc:title"]},
  {"field": "Author", "removed": true, "info": null, "info_encoding": null, "xmp": null,
   "xmp_properties": ["dc:creator"]}
]}
```

`xmp_properties` names the properties the write rewrites (those the packet
holds) or adds (the first the field pairs with, when it holds none); it is
empty, and `xmp` `null`, when the file has no packet that states anything
(none is created) or one that does not scan (it is dropped). Each original
value of a field set, of three letters or digits or more, becomes a string
the self-check searches the output for. Failures: `invalid_argument` for a
missing `fields_json` or `out`, a key outside the field list (the message
names the list; names are case-sensitive) and a value that is neither a
string nor `null`.

### 4.15 pdfkit_redact_images (catalogue 6)

`pdfkit_redact_images(edit, treatments_json, out)` adds image treatments to
the edit plan (`docs/DESIGN.md` §14); the pixels change when the plan is
written. `treatments_json` is `{"treatments": [...]}`, each treatment an
object:

| Key | Holds |
|---|---|
| `identity` | required: an identity `pdfkit_images` lists for this document |
| `treatment` | required: `box` (a filled rectangle painted into the samples), `blur` (a mosaic), `remove` (the image and every operator that draws it go), or `region` (a box over a rectangle the caller found in the pixels; several may be given per identity, beside a box or a blur) |
| `rectangle` | four numbers: in pixels `[left, top, right, bottom]` from the image's top-left corner (right and bottom exclusive, rounded outward); in page space `[left, bottom, right, top]` in default user space. Absent: the whole image (not for `region`); not with `remove` |
| `space` | `image` (default) or `page`; `page` needs `page` and `rectangle`, and the rectangle is mapped through the inverse of the CTM of every draw of the image on that page, each mapped rectangle being treated |
| `page` | the page of a page-space rectangle, from 1 |
| `caption` | `box` and `region` only: a caption drawn in the box from the placeholder alphabet (`[`, `]`, `_`, `0` to `9`, `A` to `Z`), centred, at the largest height of the caption font (8 to 64 pixels) that fits, in white or black, whichever contrasts with the fill; a box too small for 8 pixels draws none |
| `colour` | `box` and `region` only: `[red, green, blue]` in 0 to 1 (default black); the fill is the sample value of the image's own colour space and `/Decode` that reads nearest to it (an Indexed image's nearest palette entry), never one a colour-key `/Mask` hides |

One identity takes one `box`, `blur` or `remove`; a second is refused naming
the first, also across calls. A blur's block is at least 8 pixels and a
sixteenth of the image's smaller side, a multiple of 8 for a JPEG, on a grid
from the image's top-left corner. Every treatment is checked before any is
recorded, so a refused call leaves the plan as it was.

```json
{"treatments": [{"treatment": 0, "identity": "img:0123456789abcdef", "kind": "box",
                 "width": 640, "height": 480, "pixels": [[0, 0, 640, 480]], "block": null,
                 "encoded": {"sha256": "5f1c...", "bytes": 48213},
                 "occurrences": [{"page": 1, "ordinal": 0, "rectangles": [[72, 500, 300, 671]]}]}]}
```

| Key | Holds |
|---|---|
| `treatment`, `identity`, `kind` | the treatment's index in this call, its identity and kind |
| `width`, `height` | the image's size in pixels as decoded (the codec's own when it has one); 0 for a removal |
| `pixels` | the pixel rectangles painted or mosaicked, `[left, top, right, bottom]`; empty for a removal |
| `block` | the mosaic's block side for a blur, `null` otherwise |
| `encoded` | the SHA-256 and length of the image's encoded data as stored (decrypted, no filter applied), so a caller can search an output for the original |
| `occurrences` | per draw of the image (`page`, `ordinal` as `pdfkit_images` numbers them): the device rectangles the treatment changes there (`[left, bottom, right, top]`, 0.01 pt), or the draw's box for a removal |

| Failure | Status | `code` |
|---|---|---|
| a null `edit` or `out`, malformed JSON, no `treatments` array, an unknown key or treatment, a rectangle that is not four finite numbers, a page space without a page and a rectangle, a region without a rectangle, a colour outside 0 to 1 | `PDFKIT_E_INVALID_ARGUMENT` | `invalid_argument` |
| an identity no page draws; a second box, blur or remove of one identity; a region of a removed image; a caption or colour on a blur or removal; a caption outside the alphabet; a rectangle that covers no pixel; a page rectangle on a page that does not draw the image; a blur of a bitonal image under 128 pixels on its smaller side (a box is offered) | `PDFKIT_E_INVALID_ARGUMENT` | `invalid_argument` |
| a box, blur or region of an image that does not decode (the message offers `remove`) | `PDFKIT_E_REFUSED` | `undecodable_image` |
| a limit met while a page or an image is read | `PDFKIT_E_REFUSED` | `limit_exceeded` |

## 5. Versioning

- The ABI only grows: a symbol never changes its signature or its meaning, a
  JSON output only gains fields, and a status value never changes.
- `pdfkit_abi_version` returns 1 and is raised by one on every addition to the
  header (a symbol or a status value), in the change that adds it.
- A caller checks `pdfkit_abi_version() >= N` for the version whose symbols it
  binds, and reads `pdfkit_version`'s `catalogue` to learn which functions this
  build implements.

## 6. The command-line tool

`pdfkit <verb> [arguments]` writes one JSON document on standard output and
exits with a stable code. `pdfkit --help` lists the verbs, `pdfkit <verb> --help`
prints a verb's usage and an example, and `pdfkit --version` prints the
version object of §4.1. The tool calls the C ABI and nothing beneath it.

| Verb | Arguments | Catalogue functions |
|---|---|---|
| `inspect` | `<file.pdf> [--options <json>] [--open-options <json>]` | 1.2, 1.1 |
| `text` | `<file.pdf> [--pages <1-3,7>] [--glyphs] [--options <json>] [--open-options <json>]`; the flags are written into the options and exclude `--options` | 2, 1.1 |
| `images` | `<file.pdf> [--out <folder>] [--options <json>] [--open-options <json>]`; prints the JSON of `pdfkit_images`; with `--out`, writes each image as `<identity digits>.jpg` (a DCT source's stored bytes) or `.png`, and each mask as `<identity digits>.mask.png`, and adds `files` to the JSON: per file `{"identity", "file"}`, or `{"identity", "skipped"}` with the error code of an image that does not decode | 3.1, 3.2, 1.1 |
| `metadata` | `<file.pdf> [--options <json>] [--open-options <json>]` | 4, 1.1 |
| `redact` | `<job.json>` | 5, 6, 7, 9 (each section present), 10.1, 1.1 |
| `select-pages` | `<file.pdf> --pages <list> --out <output.pdf> [--open-options <json>]`, the list being page numbers and ascending ranges separated by commas (`1,3-5`) | 9, 10.1, 1.1 |
| `check` | `<file.pdf> --strings <json>` | 10.2 |
| `channels` | `<file.pdf> [--options <json>] [--open-options <json>]` | 8, 1.1 |
| `dataset` | `--file <file.pdf> --id <id> --out <folder>`, or `--manifest <manifest.tsv> --files <files root> --out <folder>` | every function this build implements (`tests/dataset/outcome.md` owns the outcome) |

The redact job file is a JSON object: `input` and `output` (paths), an optional
`open_options` object, and at least one of the sections `text`, `images`,
`metadata` and `pages`, each handed byte for byte to its function
(`pdfkit_redact_text`, `pdfkit_redact_images`, `pdfkit_set_metadata`,
`pdfkit_select_pages`) on one edit plan. An output file is written only when
`pdfkit_write` succeeds. The `text` section is the argument of
`pdfkit_redact_text` (`{"matches": [...]}`, §4.11) and the `images` section
the argument of `pdfkit_redact_images` (`{"treatments": [...]}`, §4.15); a
self-check refusal exits 3 and writes no file. A job with a section whose
function the build lacks is refused as a usage error naming the section and
the function, before the input is read.

The dataset verb writes one outcome per file into `<folder>/outcomes/<id>.json`,
a folder the caller creates (the harness does), in the shape
`tests/dataset/outcome.md` fixes, and prints `{"id", "outcome"}` for one file or
`{"files", "written"}` for a manifest. It exits 0 whenever the outcome was
written, whatever the file's fate, because the refusal is recorded inside it.

Order of work: the verb parses its arguments, reads its input file, then asks
`pdfkit_version` whether every function it needs is implemented (its own
function first) and only then opens the document, so a missing function is
reported without reading the document.

### 6.1 Exit codes

| Code | Meaning | Statuses |
|---|---|---|
| 0 | success | `PDFKIT_OK` |
| 1 | usage error: a wrong command line, a file that cannot be read or written, a malformed option | `PDFKIT_E_INVALID_ARGUMENT` |
| 2 | the input was refused; the JSON names why | `PDFKIT_E_REFUSED` |
| 3 | the self-check refused the output | `PDFKIT_E_SELF_CHECK` |
| 4 | an internal error, or a function this build lacks | `PDFKIT_E_INTERNAL`, `PDFKIT_E_NOT_IMPLEMENTED` |

`PDFKIT_E_NOT_IMPLEMENTED` maps to 4 because a missing function is a defect
from the caller's point of view: the build in hand cannot do what was asked.

### 6.2 Errors the tool finds itself

A usage error, a file the tool cannot read or write, and a mismatch between
`pdfkit.exe` and `pdfkit.dll` are reported in the four-field shape of §3, with
`code` `usage`, `output_failed` or `internal` and `failed` naming the verb:

```json
{"code":"usage","failed":"pdfkit inspect: cannot read missing.pdf: No such file or directory","expected":"the path of a readable file","fix":"check the path and that the file exists and is not open elsewhere"}
```

Paths are UTF-8: the executable's manifest sets the process code page to UTF-8,
so a path in any script reaches the file system intact.
