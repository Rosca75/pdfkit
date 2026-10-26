# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The image treatment specimens. treat_suite writes three files of three
# pages each: an RGB image drawn on every page and, on page 3, inside a form;
# an inline image, an image with a soft mask and a line of text on page 1; a
# colour-key image, a stencil and an Indexed image on page 2; a 16-bit image
# on page 3; every image large enough for a caption at the font's smallest
# height and for a region box. treat_codec writes one file per codec source,
# the image drawn the same way on three pages and inside a form: its encoded
# bytes are copied from a dataset file whose licence permits it (the file is
# read by its manifest hash, so the bytes cannot change), and the specimen
# carries that file's licence. The images' samples are deterministic
# gradients, so a second run writes the same bytes.

from __future__ import annotations

import hashlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import autotag  # noqa: E402
import common  # noqa: E402
from synth.patterns.images import gradient  # noqa: E402
from synth.patterns.kit import Name, Pdf, Specimen, Stream, helvetica, info, ops, page, show  # noqa: E402

CAPTION = "Treatment specimen {n} page {p}"
TREATMENT_TAGS = ["treat_region", "treat_caption", "shared_image", "image_in_form"]

# The codec sources: (pattern suffix, manifest id, image object, globals
# object or None, the image dictionary written for it, its tags). Each
# dictionary restates what the source states (checked against its bytes).
CODECS = (
    ("jbig2", "pdfium-0015", 5, 4,
     {"Width": 1152, "Height": 720, "ColorSpace": Name("DeviceGray"), "BitsPerComponent": 1,
      "Filter": Name("JBIG2Decode")},
     ["img_jbig2", "img_jbig2_globals", "cs_gray", "bpc_1"]),
    ("cmyk_jpeg", "pdfpig-0009", 1205, None,
     {"Width": 240, "Height": 240, "ColorSpace": Name("DeviceCMYK"), "BitsPerComponent": 8,
      "Filter": Name("DCTDecode")},
     ["img_dct", "img_dct_cmyk", "cs_cmyk"]),
    ("ccitt_g4", "opf-0007", 4, None,
     {"Width": 52, "Height": 49, "ImageMask": True, "BitsPerComponent": 1,
      "Filter": Name("CCITTFaxDecode"), "DecodeParms": {"K": -1, "Columns": 52}},
     ["img_ccitt_g4", "stencil", "bpc_1"]),
    ("jpx", "nicar25-0003", 6, None,
     {"Width": 751, "Height": 1112, "BitsPerComponent": 8, "Filter": Name("JPXDecode")},
     ["img_jpx"]),
)


def image(pdf: Pdf, width: int, height: int, colour_space, bits: int, samples: bytes, **extra):
    entries = {"Type": Name("XObject"), "Subtype": Name("Image"), "Width": width, "Height": height,
               "BitsPerComponent": bits}
    if colour_space is not None:
        entries["ColorSpace"] = colour_space
    entries.update(extra)
    return pdf.add(Stream(samples, entries))


def three_pages(pdf: Pdf, n: int, shared, extra_draws: list[bytes], extra_xobjects: list[dict]) -> list[dict]:
    """Three pages drawing `shared` (the third inside a form) and each page's extra draws."""
    font = pdf.add(helvetica())
    form = pdf.add(Stream(b"q 160 0 0 120 0 0 cm /Sh Do Q", {
        "Type": Name("XObject"), "Subtype": Name("Form"), "BBox": [0, 0, 160, 120],
        "Resources": {"XObject": {"Sh": shared}}}))
    pages = []
    for p in range(3):
        text = b"BT /F1 12 Tf 72 740 Td " + show(CAPTION.format(n=n, p=p + 1)) + b" Tj ET"
        draw = (b"q 1 0 0 1 72 420 cm /Fm Do Q" if p == 2 else
                f"q 200 0 0 150 {72 + 40 * p} 420 cm /Sh Do Q".encode())
        xobjects = {"Sh": shared, "Fm": form} if p == 2 else {"Sh": shared}
        xobjects.update(extra_xobjects[p])
        pages.append(page(pdf, ops(text, draw, extra_draws[p]), {"Font": {"F1": font}, "XObject": xobjects}))
    return pages


def suite(count: int = 3) -> list[Specimen]:
    out = []
    for n in range(count):
        pdf = Pdf()
        shared = image(pdf, 64, 48, Name("DeviceRGB"), 8, gradient(64, 48, 3, seed=n))
        alpha = image(pdf, 48, 48, Name("DeviceGray"), 8, gradient(48, 48, 1, seed=70 + n))
        masked = image(pdf, 48, 48, Name("DeviceRGB"), 8, gradient(48, 48, 3, seed=20 + n), SMask=alpha)
        keyed = image(pdf, 48, 32, Name("DeviceRGB"), 8, gradient(48, 32, 3, seed=30 + n),
                      Mask=[0, 60, 0, 255, 0, 255])
        stencil = image(pdf, 64, 64, None, 1, gradient(64, 64, 1, 1, seed=40 + n), ImageMask=True)
        palette = bytes(v for i in range(16) for v in (i * 16, 255 - i * 16, (i * 40) & 0xFF))
        indexed = image(pdf, 40, 40, [Name("Indexed"), Name("DeviceRGB"), 15, palette], 8,
                        bytes((x + y + n) % 16 for y in range(40) for x in range(40)))
        deep = image(pdf, 32, 32, Name("DeviceGray"), 16, gradient(32, 32, 1, 16, seed=50 + n))
        inline = b"BI /W 32 /H 16 /CS /G /BPC 8 /F /AHx ID " + gradient(32, 16, 1, seed=60 + n).hex().encode() + b"> EI"
        draws = [
            ops(b"q 120 0 0 60 340 600 cm " + inline + b" Q", b"q 96 0 0 96 340 450 cm /Ms Do Q"),
            ops(b"q 96 0 0 64 340 600 cm /Ky Do Q", b"0 0.4 0.8 rg q 96 0 0 96 340 450 cm /St Do Q",
                b"q 80 0 0 80 340 300 cm /Ix Do Q"),
            ops(b"q 64 0 0 64 340 600 cm /Dp Do Q"),
        ]
        extra = [{"Ms": masked}, {"Ky": keyed, "St": stencil, "Ix": indexed}, {"Dp": deep}]
        pdf.document(three_pages(pdf, n + 1, shared, draws, extra), None, info("treat_suite"))
        text = "\n".join(CAPTION.format(n=n + 1, p=p + 1) for p in range(3))
        out.append(Specimen(pdf.build(), TREATMENT_TAGS + [
            "inline_image", "smask", "colour_key", "stencil", "cs_indexed", "bpc_16", "bpc_1", "img_flate",
            "cs_rgb", "cs_gray"], text, notes="every image case of the treatment job in one file"))
    return out


def source_object(row_id: str, number: int) -> tuple[bytes, bytes]:
    """The dictionary text and the raw stream bytes of object `number` of a dataset file."""
    row = next((r for r in common.read_manifest() if r["id"] == row_id), None)
    if row is None:
        raise common.ToolError(f"{row_id} is not in the manifest", "a manifest row to copy a codec stream from",
                               "restore the row or change CODECS in synth/images/treatments.py")
    data = common.file_path(row).read_bytes()
    if hashlib.sha256(data).hexdigest() != row["sha256"]:
        raise common.ToolError(f"{row_id}'s file does not have its manifest hash", "the file the manifest pins",
                               "run `python dataset/tools/fetch.py` to restore the dataset")
    for obj in autotag.scan_objects(data):
        if obj.num == number and obj.data is not None:
            return obj.head, obj.data
    raise common.ToolError(f"{row_id} has no stream object {number}", "the codec stream CODECS names",
                           "correct CODECS in synth/images/treatments.py")


def codec(suffix: str, row_id: str, number: int, globals_number, entries: dict, tags: list[str]):
    def build() -> list[Specimen]:
        head, data = source_object(row_id, number)
        if str(entries["Filter"]).encode() not in head:
            raise common.ToolError(f"{row_id} object {number} is not {entries['Filter']}",
                                   "the codec CODECS names", "correct CODECS in synth/images/treatments.py")
        pdf = Pdf()
        parameters = dict(entries)
        if globals_number is not None:
            _, globals_data = source_object(row_id, globals_number)
            parameters["DecodeParms"] = {"JBIG2Globals": pdf.add(Stream(globals_data, {}, compress=False))}
        filters = [parameters.pop("Filter")]
        stream_entries = {"Type": Name("XObject"), "Subtype": Name("Image")}
        stream_entries.update(parameters)
        shared = pdf.add(Stream(data, stream_entries, compress=False, filters=filters))
        # Two more identities of the same bytes, told apart by an explicit
        # /Decode (the default, then inverted), so the treatment cycle gives
        # the codec's samples a box, a blur and a removal in one file.
        components = {"DeviceGray": 1, "DeviceCMYK": 4}.get(str(entries.get("ColorSpace")), 1)
        if "ColorSpace" not in entries and not entries.get("ImageMask"):
            components = 3
        variants = []
        for decode in ([0, 1] * components, [1, 0] * components):
            variant = dict(stream_entries, Decode=decode)
            variants.append(pdf.add(Stream(data, variant, compress=False, filters=filters)))
        draws = [b"q 120 0 0 90 340 600 cm /Va Do Q", b"q 120 0 0 90 340 600 cm /Vb Do Q", b""]
        extra = [{"Va": variants[0]}, {"Vb": variants[1]}, {}]
        pdf.document(three_pages(pdf, 1, shared, draws, extra), None, info(f"treat_{suffix}"))
        text = "\n".join(CAPTION.format(n=1, p=p + 1) for p in range(3))
        row = next(r for r in common.read_manifest() if r["id"] == row_id)
        return [Specimen(pdf.build(), TREATMENT_TAGS + tags, text,
                         notes=f"the {suffix} stream of {row_id} object {number}, drawn on three pages",
                         licence=row["licence"])]
    return build


PATTERNS = {"treat_suite": suite}
for _suffix, _row, _number, _globals, _entries, _tags in CODECS:
    PATTERNS[f"treat_{_suffix}"] = codec(_suffix, _row, _number, _globals, _entries, _tags)
