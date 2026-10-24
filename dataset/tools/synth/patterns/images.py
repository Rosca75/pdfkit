# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Patterns for the image cases of docs/DESIGN.md §9: inline images (raw, hex
# and Flate), stencil masks, colour-key masks, soft masks, one image shared by
# two pages, an image inside a form XObject, the LZW and RunLength filters, and
# the Lab, Separation, DeviceN and Indexed colour spaces, at 1, 8 and 16 bits.
# The LZW encoder clears its table before the code width would grow, which every
# decoder reads the same whatever its EarlyChange handling (ISO 32000-1 §7.4.4).

from __future__ import annotations

import zlib

from synth.patterns.kit import Name, Pdf, Specimen, Stream, helvetica, info, ops, page, show

CAPTION = "Figure {n} shows a generated test image"


def gradient(width: int, height: int, components: int, bits: int = 8, seed: int = 0) -> bytes:
    """A deterministic gradient, row by row."""
    if bits == 1:
        row_bytes = (width + 7) // 8
        return bytes(((x * 37 + y * 11 + seed) & 0xFF) for y in range(height) for x in range(row_bytes))
    out = bytearray()
    for y in range(height):
        for x in range(width):
            for c in range(components):
                value = (x * 255 // max(1, width - 1) + c * 85 + y * 3 + seed) & 0xFF
                if bits == 16:
                    out += bytes([value, (value * 7) & 0xFF])
                else:
                    out.append(value)
    return bytes(out)


def lzw_encode(data: bytes) -> bytes:
    """LZW with 9-bit codes only: the table is cleared before it reaches 511 entries."""
    bits = []

    def emit(code):
        bits.append(format(code, "09b"))

    table = {bytes([i]): i for i in range(256)}
    next_code = 258
    emit(256)
    word = b""
    for byte in data:
        candidate = word + bytes([byte])
        if candidate in table:
            word = candidate
            continue
        emit(table[word])
        table[candidate] = next_code
        next_code += 1
        word = bytes([byte])
        if next_code >= 500:
            emit(256)
            table = {bytes([i]): i for i in range(256)}
            next_code = 258
    if word:
        emit(table[word])
    emit(257)
    stream = "".join(bits)
    stream += "0" * (-len(stream) % 8)
    return bytes(int(stream[i:i + 8], 2) for i in range(0, len(stream), 8))


def runlength_encode(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(data):
        run = 1
        while i + run < len(data) and run < 128 and data[i + run] == data[i]:
            run += 1
        if run > 1:
            out += bytes([257 - run, data[i]])
            i += run
            continue
        start = i
        while i < len(data) and i - start < 128 and (i + 1 >= len(data) or data[i + 1] != data[i]):
            i += 1
        if i == start:
            i += 1
        out += bytes([i - start - 1]) + data[start:i]
    out.append(128)
    return bytes(out)


def image(pdf: Pdf, width: int, height: int, colour_space, bits: int, samples: bytes,
          filters=None, encoded: bytes | None = None, **extra):
    entries = {"Type": Name("XObject"), "Subtype": Name("Image"), "Width": width, "Height": height,
               "BitsPerComponent": bits}
    if colour_space is not None:
        entries["ColorSpace"] = colour_space
    entries.update(extra)
    if filters is None:
        return pdf.add(Stream(samples, entries))
    return pdf.add(Stream(encoded, entries, compress=False, filters=filters))


def _doc(tag_list, note, build, count=3):
    out = []
    for i in range(count):
        pdf = Pdf()
        font = pdf.add(helvetica())
        res = {"Font": {"F1": font}, "XObject": {}}
        draws, pages_extra = build(i, pdf, res)
        caption = CAPTION.format(n=i + 1)
        content = ops(b"BT /F1 12 Tf 72 740 Td " + show(caption) + b" Tj ET", *draws)
        pages = [page(pdf, content, res)]
        for extra_content in pages_extra:
            pages.append(page(pdf, ops(b"BT /F1 12 Tf 72 740 Td " + show(caption) + b" Tj ET", extra_content), res))
        pdf.document(pages, None, info(tag_list[0]))
        text = "\n".join([caption] * len(pages))
        out.append(Specimen(pdf.build(), list(tag_list), text, notes=note))
    return out


def _place(name: str, x=72, y=500, w=200, h=150) -> bytes:
    return f"q {w} 0 0 {h} {x} {y} cm /{name} Do Q".encode()


def inline_image(count=3):
    def build(i, pdf, res):
        w, h = 16, 8
        if i == 0:
            body = b"BI /W 16 /H 8 /CS /G /BPC 8 ID " + gradient(w, h, 1) + b" EI"
        elif i == 1:
            body = b"BI /W 16 /H 8 /CS /RGB /BPC 8 /F /AHx ID " + gradient(w, h, 3).hex().encode() + b"> EI"
        else:
            body = b"BI /W 16 /H 8 /CS /RGB /BPC 8 /F /Fl ID " + zlib.compress(gradient(w, h, 3, seed=9)) + b" EI"
        return [b"q 200 0 0 100 72 500 cm " + body + b" Q"], []
    return _doc(["inline_image"], "an inline image (BI ID EI)", build, count)


def stencil(count=3):
    def build(i, pdf, res):
        im = image(pdf, 32, 32, None, 1, gradient(32, 32, 1, 1, seed=i), ImageMask=True)
        res["XObject"]["Im1"] = im
        colour = ("0.8 0 0 rg", "0 0.5 0 rg", "0 0 0.7 rg")[i % 3]
        return [colour.encode() + b" " + _place("Im1")], []
    return _doc(["stencil", "bpc_1"], "a stencil mask painted in the fill colour", build, count)


def colour_key(count=3):
    def build(i, pdf, res):
        im = image(pdf, 24, 24, Name("DeviceRGB"), 8, gradient(24, 24, 3, seed=i),
                   Mask=[0, 120, 0, 255, 0, 255])
        res["XObject"]["Im1"] = im
        return [_place("Im1")], []
    return _doc(["colour_key", "img_flate", "cs_rgb"], "an image with a /Mask colour-key array", build, count)


def smask(count=2):
    def build(i, pdf, res):
        alpha = image(pdf, 24, 24, Name("DeviceGray"), 8, gradient(24, 24, 1, seed=50 + i))
        im = image(pdf, 24, 24, Name("DeviceRGB"), 8, gradient(24, 24, 3, seed=i), SMask=alpha)
        res["XObject"]["Im1"] = im
        return [_place("Im1")], []
    return _doc(["smask", "img_flate", "cs_rgb", "cs_gray"], "an RGB image with a soft mask", build, count)


def shared_image(count=3):
    def build(i, pdf, res):
        im = image(pdf, 20, 20, Name("DeviceRGB"), 8, gradient(20, 20, 3, seed=i))
        res["XObject"]["Im1"] = im
        return [_place("Im1")], [_place("Im1", 300, 300, 100, 100)]
    return _doc(["shared_image", "img_flate", "cs_rgb"], "one image XObject drawn on two pages", build, count)


def image_in_form(count=3):
    def build(i, pdf, res):
        im = image(pdf, 20, 20, Name("DeviceGray"), 8, gradient(20, 20, 1, seed=i))
        form = pdf.add(Stream(b"q 100 0 0 100 0 0 cm /Im1 Do Q", {
            "Type": Name("XObject"), "Subtype": Name("Form"), "BBox": [0, 0, 100, 100],
            "Resources": {"XObject": {"Im1": im}}}))
        res["XObject"]["Fm1"] = form
        return [b"q 1 0 0 1 72 400 cm /Fm1 Do Q", b"q 1 0 0 1 300 400 cm /Fm1 Do Q"], []
    return _doc(["image_in_form", "img_flate", "cs_gray"], "an image drawn from inside a form XObject (twice)",
                build, count)


def img_lzw(count=3):
    def build(i, pdf, res):
        samples = gradient(32, 16, 3, seed=i)
        im = image(pdf, 32, 16, Name("DeviceRGB"), 8, samples, filters=[Name("LZWDecode")],
                   encoded=lzw_encode(samples))
        res["XObject"]["Im1"] = im
        return [_place("Im1")], []
    return _doc(["img_lzw", "cs_rgb"], "an image with /LZWDecode", build, count)


def img_runlength(count=3):
    def build(i, pdf, res):
        samples = bytes(((x // 8 + y // 4 + i) % 2) * 255 for y in range(16) for x in range(32))
        im = image(pdf, 32, 16, Name("DeviceGray"), 8, samples, filters=[Name("RunLengthDecode")],
                   encoded=runlength_encode(samples))
        res["XObject"]["Im1"] = im
        return [_place("Im1")], []
    return _doc(["img_runlength", "cs_gray"], "an image with /RunLengthDecode", build, count)


def cs_lab(count=3):
    def build(i, pdf, res):
        lab = [Name("Lab"), {"WhitePoint": [0.9505, 1, 1.089], "Range": [-100, 100, -100, 100]}]
        im = image(pdf, 24, 24, lab, 8, gradient(24, 24, 3, seed=i))
        res["XObject"]["Im1"] = im
        return [_place("Im1")], []
    return _doc(["cs_lab", "img_flate"], "an image in a Lab colour space", build, count)


def cs_separation(count=2):
    def build(i, pdf, res):
        tint = {"FunctionType": 2, "Domain": [0, 1], "C0": [0, 0, 0, 0], "C1": [0, 1, 0.2, 0], "N": 1}
        sep = [Name("Separation"), Name("SpotRed"), Name("DeviceCMYK"), tint]
        im = image(pdf, 24, 24, sep, 8, gradient(24, 24, 1, seed=i))
        res["XObject"]["Im1"] = im
        return [_place("Im1")], []
    return _doc(["cs_separation", "img_flate"], "an image in a Separation colour space", build, count)


def cs_devicen(count=2):
    def build(i, pdf, res):
        fn = pdf.add(Stream(b"{0 0}", {"FunctionType": 4, "Domain": [0, 1, 0, 1],
                                        "Range": [0, 1, 0, 1, 0, 1, 0, 1]}, compress=False))
        devn = [Name("DeviceN"), [Name("Cyan"), Name("Magenta")], Name("DeviceCMYK"), fn]
        im = image(pdf, 24, 24, devn, 8, gradient(24, 24, 2, seed=i))
        res["XObject"]["Im1"] = im
        return [_place("Im1")], []
    return _doc(["cs_devicen", "img_flate"], "an image in a DeviceN colour space", build, count)


def cs_indexed(count=1):
    def build(i, pdf, res):
        palette = bytes(v for n in range(16) for v in (n * 16, 255 - n * 16, (n * 40) & 0xFF))
        im = image(pdf, 16, 16, [Name("Indexed"), Name("DeviceRGB"), 15, palette], 8,
                   bytes((x + y) % 16 for y in range(16) for x in range(16)))
        res["XObject"]["Im1"] = im
        return [_place("Im1")], []
    return _doc(["cs_indexed", "img_flate"], "an image in an Indexed colour space", build, count)


def bpc_16(count=2):
    def build(i, pdf, res):
        im = image(pdf, 16, 16, Name("DeviceGray"), 16, gradient(16, 16, 1, 16, seed=i))
        res["XObject"]["Im1"] = im
        return [_place("Im1")], []
    return _doc(["bpc_16", "cs_gray", "img_flate"], "a 16-bit grey image", build, count)


PATTERNS = {
    "inline_image": inline_image,
    "stencil": stencil,
    "colour_key": colour_key,
    "smask": smask,
    "shared_image": shared_image,
    "image_in_form": image_in_form,
    "img_lzw": img_lzw,
    "img_runlength": img_runlength,
    "cs_lab": cs_lab,
    "cs_separation": cs_separation,
    "cs_devicen": cs_devicen,
    "cs_indexed": cs_indexed,
    "bpc_16": bpc_16,
}
