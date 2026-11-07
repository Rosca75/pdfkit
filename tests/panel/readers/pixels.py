# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The one form every pixel member's answer takes, so the judge compares images
# without knowing which tool wrote them: width, height, a colour layout (`gray`,
# `rgb`, `rgba` or `cmyk`) and the samples, eight bits each, rows top to bottom
# with no padding. The members write PBM, PGM and PPM (Poppler's pdfimages, by
# default), PAM and PNG (MuPDF's mutool extract) and PDFium's BGRA bitmaps; this
# module reads each into that form. A PBM sample 1 is black (Netpbm's pbm(5)),
# so a bilevel image becomes gray 0 for black and 255 for white.
#
# Sources: the Netpbm formats pbm(5), pgm(5), ppm(5) and pam(5)
# (https://netpbm.sourceforge.net/doc/) and the PNG specification's chunk
# layout, colour types, bit depths and five row filters (None, Sub, Up,
# Average, Paeth; https://www.w3.org/TR/png-3/), both recalled; tested by
# tests/panel/test_readers.py and on the members' outputs of synth-0263,
# synth-0265, qpdf-0026 and dccc-0003.

from __future__ import annotations

import struct
import zlib

LAYOUTS = {"gray": 1, "rgb": 3, "rgba": 4, "cmyk": 4}
# A PBM byte as its eight gray samples, most significant bit first: 1 is black.
_BITS = [bytes(0 if (byte >> (7 - i)) & 1 else 255 for i in range(8)) for byte in range(256)]
_INVERT = bytes(255 - i for i in range(256))


class PixelError(ValueError):
    """A member's output that is not an image this module reads."""


def image(width: int, height: int, layout: str, samples: bytes, source: str) -> dict:
    if layout not in LAYOUTS:
        raise PixelError(f"unknown layout {layout}")
    if len(samples) != width * height * LAYOUTS[layout]:
        raise PixelError(f"{source}: {len(samples)} samples for {width}x{height} {layout}")
    return {"width": width, "height": height, "layout": layout, "samples": samples, "source": source}


def _pnm_tokens(data: bytes, count: int) -> tuple[list[int], int]:
    """The first `count` header numbers of a PNM file and the offset of its raster."""
    values, pos = [], 2
    while len(values) < count:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while pos < len(data) and data[pos:pos + 1] not in (b"\n", b"\r"):
                pos += 1
            continue
        start = pos
        while pos < len(data) and data[pos:pos + 1].isdigit():
            pos += 1
        if start == pos:
            raise PixelError("a PNM header that is not numeric")
        values.append(int(data[start:pos]))
    return values, pos + 1  # one whitespace byte ends the header


def read_pnm(data: bytes) -> dict:
    """A binary PBM (P4), PGM (P5) or PPM (P6) of eight-bit samples, or a PAM (P7)."""
    magic = data[:2]
    if magic == b"P7":
        return read_pam(data)
    if magic == b"P4":
        (width, height), pos = _pnm_tokens(data, 2)
        stride = (width + 7) // 8
        raster = data[pos:pos + stride * height]
        if len(raster) != stride * height:
            raise PixelError("a PBM raster shorter than its header")
        expanded = b"".join(_BITS[b] for b in raster)  # 8 samples per byte, padding included
        samples = b"".join(expanded[y * stride * 8: y * stride * 8 + width] for y in range(height))
        return image(width, height, "gray", samples, "pbm")
    if magic in (b"P5", b"P6"):
        (width, height, maxval), pos = _pnm_tokens(data, 3)
        channels = 1 if magic == b"P5" else 3
        if maxval != 255:
            raise PixelError(f"a PNM of maximum value {maxval}; only 255 is read")
        samples = data[pos:pos + width * height * channels]
        return image(width, height, "gray" if channels == 1 else "rgb", samples,
                     "pgm" if channels == 1 else "ppm")
    raise PixelError(f"not a binary PNM: {magic!r}")


def read_pam(data: bytes) -> dict:
    """A PAM (P7) of eight-bit samples: GRAYSCALE, RGB, RGB_ALPHA or CMYK."""
    end = data.find(b"ENDHDR\n")
    if end < 0:
        raise PixelError("a PAM without ENDHDR")
    fields = {}
    for line in data[3:end].decode("ascii", "replace").splitlines():
        parts = line.split(None, 1)
        if len(parts) == 2 and not line.startswith("#"):
            fields[parts[0]] = parts[1].strip()
    width, height = int(fields["WIDTH"]), int(fields["HEIGHT"])
    depth, maxval = int(fields["DEPTH"]), int(fields["MAXVAL"])
    if maxval != 255:
        raise PixelError(f"a PAM of maximum value {maxval}; only 255 is read")
    kind = fields.get("TUPLTYPE", "")
    layout = {(1, "GRAYSCALE"): "gray", (3, "RGB"): "rgb", (4, "RGB_ALPHA"): "rgba",
              (4, "CMYK"): "cmyk"}.get((depth, kind))
    if layout is None:
        raise PixelError(f"a PAM of depth {depth} and tuple type {kind or 'none'}")
    pos = end + len(b"ENDHDR\n")
    return image(width, height, layout, data[pos:pos + width * height * depth], "pam")


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def _unfilter(raw: bytes, height: int, stride: int, bpp: int) -> bytes:
    out = bytearray(height * stride)
    previous = bytearray(stride)
    pos = 0
    for y in range(height):
        kind = raw[pos]
        row = bytearray(raw[pos + 1: pos + 1 + stride])
        pos += 1 + stride
        if kind == 1:
            for i in range(bpp, stride):
                row[i] = (row[i] + row[i - bpp]) & 0xFF
        elif kind == 2:
            row = bytearray((r + p) & 0xFF for r, p in zip(row, previous))
        elif kind == 3:
            for i in range(stride):
                left = row[i - bpp] if i >= bpp else 0
                row[i] = (row[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif kind == 4:
            for i in range(stride):
                left = row[i - bpp] if i >= bpp else 0
                corner = previous[i - bpp] if i >= bpp else 0
                row[i] = (row[i] + _paeth(left, previous[i], corner)) & 0xFF
        elif kind != 0:
            raise PixelError(f"PNG row filter {kind}")
        out[y * stride:(y + 1) * stride] = row
        previous = row
    return bytes(out)


def read_png(data: bytes) -> dict:
    """A non-interlaced PNG of bit depth 1, 2, 4, 8 or 16, any colour type; a palette image is
    expanded through its palette, 16-bit samples keep their high byte."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise PixelError("not a PNG")
    pos, header, palette, transparency, compressed = 8, None, None, None, []
    while pos + 8 <= len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            palette = body
        elif kind == b"tRNS":
            transparency = body
        elif kind == b"IDAT":
            compressed.append(body)
        elif kind == b"IEND":
            break
    if header is None:
        raise PixelError("a PNG without IHDR")
    width, height, depth, colour, _, _, interlace = header
    if interlace:
        raise PixelError("an interlaced PNG")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colour]
    bits = channels * depth
    stride = (width * bits + 7) // 8
    raw = _unfilter(zlib.decompress(b"".join(compressed)), height, stride, max(1, bits // 8))
    if depth == 16:
        raw = raw[0::2]
        stride //= 2
    elif depth < 8:
        expanded = bytearray(width * height)
        scale = 255 // ((1 << depth) - 1) if colour != 3 else 1
        per = 8 // depth
        mask = (1 << depth) - 1
        for y in range(height):
            row = raw[y * stride:(y + 1) * stride]
            for x in range(width):
                value = (row[x // per] >> ((per - 1 - x % per) * depth)) & mask
                expanded[y * width + x] = value * scale
        raw = bytes(expanded)
    if colour == 3:
        if palette is None:
            raise PixelError("a palette PNG without PLTE")
        alpha = transparency or b""
        if alpha:
            out = bytearray()
            for index in raw:
                out += palette[index * 3:index * 3 + 3] + bytes([alpha[index] if index < len(alpha) else 255])
            return image(width, height, "rgba", bytes(out), "png")
        return image(width, height, "rgb", b"".join(palette[i * 3:i * 3 + 3] for i in raw), "png")
    if colour == 4:  # gray with alpha: kept as RGBA so no layout is lost
        out = bytearray()
        for i in range(0, len(raw), 2):
            out += bytes((raw[i], raw[i], raw[i], raw[i + 1]))
        return image(width, height, "rgba", bytes(out), "png")
    layout = {0: "gray", 2: "rgb", 6: "rgba"}[colour]
    return image(width, height, layout, raw, "png")


def read_any(data: bytes) -> dict:
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        return read_png(data)
    if data[:1] == b"P":
        return read_pnm(data)
    raise PixelError(f"an image format this module does not read: {data[:4]!r}")


def from_bgra(width: int, height: int, stride: int, data: bytes, fmt: int) -> dict:
    """A PDFium bitmap (fpdfview.h: 1 gray, 2 BGR, 3 BGRx, 4 BGRA) in the common form; a bitmap
    with alpha is composited over white, as a viewer shows it on a white page."""
    per = {1: 1, 2: 3, 3: 4, 4: 4}.get(fmt)
    if per is None:
        raise PixelError(f"PDFium bitmap format {fmt}")
    rows = b"".join(data[y * stride: y * stride + width * per] for y in range(height))
    if fmt == 1:
        return image(width, height, "gray", rows, "bitmap")
    # A strided copy per channel rather than a loop per pixel.
    blue, green, red = rows[0::per], rows[1::per], rows[2::per]
    if fmt == 4:
        alpha = rows[3::4]
        red, green, blue = (over_white(channel, alpha) for channel in (red, green, blue))
    rgb = bytearray(width * height * 3)
    rgb[0::3], rgb[1::3], rgb[2::3] = red, green, blue
    return image(width, height, "rgb", bytes(rgb), "bitmap")


def over_white(channel: bytes, alpha: bytes) -> bytes:
    """One channel composited over white: c * a / 255 + 255 * (1 - a / 255), rounded. An alpha of
    only 0 and 255 (an opaque image, a stencil mask) takes a bitwise path: c OR (255 - a)."""
    if set(alpha) <= {0, 255}:
        size = len(channel)
        merged = int.from_bytes(channel, "big") | int.from_bytes(alpha.translate(_INVERT), "big")
        return merged.to_bytes(size, "big")
    return bytes((c * a + 255 * (255 - a) + 127) // 255 for c, a in zip(channel, alpha))


def compact(img: dict) -> dict:
    """An RGB image whose every pixel is neutral (R = G = B) as gray, so a bilevel or gray image
    is kept and compared at one sample per pixel whichever member wrote it."""
    if img["layout"] == "rgb":
        samples = img["samples"]
        red = samples[0::3]
        if red == samples[1::3] and red == samples[2::3]:
            return dict(img, layout="gray", samples=red)
    return img
