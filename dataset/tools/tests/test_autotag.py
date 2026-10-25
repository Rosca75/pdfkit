# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Tests of the scanner on files written in code whose tags are known: the cross-
# reference forms, a clean file carrying no damage tag, fonts and encodings,
# images and inline images, render modes, channels and the generator mapping.

from __future__ import annotations

import unittest
import zlib

import support
from support import Name, Pdf, Stream, helvetica, ops, show

import autotag


def tags_of(data: bytes) -> set:
    return set(autotag.autotag(data)[0])


DAMAGE = {"xref_missing", "xref_wrong_offsets", "startxref_wrong", "eof_missing", "junk_before_header",
          "truncated", "two_trailers", "incremental_update"}


class StructureTest(unittest.TestCase):
    def test_clean_files_carry_no_damage(self):
        for xref in ("table", "stream", "hybrid"):
            with self.subTest(xref):
                self.assertFalse(tags_of(support.clean_pdf(xref=xref)) & DAMAGE)

    def test_xref_kinds(self):
        self.assertIn("xref_stream", tags_of(support.clean_pdf(xref="stream")))
        self.assertIn("hybrid_xref", tags_of(support.clean_pdf(xref="hybrid")))
        self.assertNotIn("xref_stream", tags_of(support.clean_pdf(xref="table")))


class FontTest(unittest.TestCase):
    def one_font(self, font: dict, descriptor: dict | None = None) -> set:
        pdf = Pdf()
        if descriptor is not None:
            font["FontDescriptor"] = pdf.add(descriptor)
        ref = pdf.add(font)
        content = pdf.add(Stream(ops(b"BT /F1 12 Tf 72 700 Td (Ab) Tj ET")))
        pdf.document([{"Resources": {"Font": {"F1": ref}}, "Contents": content}])
        return tags_of(pdf.build())

    def test_differences_name_classes(self):
        diffs = [65, Name("uni0041"), Name("g12"), Name("C66"), Name("Aacute")]
        tags = self.one_font({"Type": Name("Font"), "Subtype": Name("Type1"), "BaseFont": Name("Helvetica"),
                              "Encoding": {"Type": Name("Encoding"), "Differences": diffs}})
        self.assertTrue({"differences_uni", "differences_gnn", "differences_cdd", "differences_aglnames"} <= tags)
        self.assertIn("tounicode_missing", tags)

    def test_type0_identity_and_predefined(self):
        for encoding, tag in (("Identity-H", "identity_cid"), ("UniJIS-UCS2-V", "predefined_cmap")):
            with self.subTest(encoding):
                tags = self.one_font({"Type": Name("Font"), "Subtype": Name("Type0"), "BaseFont": Name("X"),
                                      "Encoding": Name(encoding), "DescendantFonts": []})
                self.assertIn(tag, tags)
        self.assertIn("vertical", self.one_font({"Type": Name("Font"), "Subtype": Name("Type0"),
                                                 "BaseFont": Name("X"), "Encoding": Name("Identity-V")}))

    def test_type3_and_not_embedded(self):
        self.assertIn("type3", self.one_font({"Type": Name("Font"), "Subtype": Name("Type3")}))
        tags = self.one_font({"Type": Name("Font"), "Subtype": Name("TrueType"), "BaseFont": Name("Verdana")},
                             {"Type": Name("FontDescriptor"), "FontName": Name("Verdana"), "Flags": 32})
        self.assertIn("font_not_embedded", tags)
        self.assertNotIn("font_not_embedded", tags_of(support.clean_pdf()))


class ImageAndContentTest(unittest.TestCase):
    def page_with(self, content: bytes, xobjects: dict | None = None, pdf: Pdf | None = None) -> bytes:
        pdf = pdf or Pdf()
        res = {"Font": {"F1": pdf.add(helvetica())}, "XObject": xobjects or {}}
        pdf.document([{"Resources": res, "Contents": pdf.add(Stream(content))}])
        return pdf.build()

    def test_image_filters_and_colour_spaces(self):
        pdf = Pdf()
        dct = pdf.add(Stream(b"\xff\xd8\xff\xd9", {"Type": Name("XObject"), "Subtype": Name("Image"),
                                                   "Width": 1, "Height": 1, "BitsPerComponent": 8,
                                                   "ColorSpace": Name("DeviceCMYK")},
                             compress=False, filters=[Name("DCTDecode")]))
        g4 = pdf.add(Stream(b"\x00", {"Type": Name("XObject"), "Subtype": Name("Image"), "Width": 8,
                                      "Height": 1, "ImageMask": True, "DecodeParms": {"K": -1}},
                            compress=False, filters=[Name("CCITTFaxDecode")]))
        lab = pdf.add(Stream(bytes(3), {"Type": Name("XObject"), "Subtype": Name("Image"), "Width": 1,
                                        "Height": 1, "BitsPerComponent": 16, "ColorSpace": [Name("Lab"), {}]}))
        tags = tags_of(self.page_with(b"/I1 Do /I2 Do /I3 Do", {"I1": dct, "I2": g4, "I3": lab}, pdf))
        self.assertTrue({"img_dct", "img_dct_cmyk", "cs_cmyk", "img_ccitt_g4", "stencil", "bpc_1",
                         "cs_lab", "bpc_16", "img_flate"} <= tags)

    def test_inline_image_and_render_modes(self):
        content = ops(b"BI /W 2 /H 1 /CS /G /BPC 8 /F /AHx ID 00FF> EI",
                      b"BT /F1 12 Tf 3 Tr 72 700 Td " + show("hidden") + b" Tj ET")
        tags = tags_of(self.page_with(content))
        self.assertTrue({"inline_image", "cs_gray", "render_invisible"} <= tags)

    def test_content_is_read_through_flate(self):
        data = self.page_with(ops(b"BT /F1 12 Tf 7 Tr (x) Tj ET"))
        self.assertIn(zlib.compress(ops(b"BT /F1 12 Tf 7 Tr (x) Tj ET"), 9), data)
        self.assertIn("render_invisible", tags_of(data))


class ChannelAndGeneratorTest(unittest.TestCase):
    def test_channels(self):
        pdf = Pdf()
        outline = pdf.add({"Type": Name("Outlines"), "Count": 0})
        pdf.add({"Orphan": True})
        annot = pdf.add({"Type": Name("Annot"), "Subtype": Name("Redact"), "Rect": [0, 0, 1, 1]})
        content = pdf.add(Stream(b"BT ET"))
        pdf.document([{"Resources": {}, "Contents": content, "Annots": [annot]}],
                     {"Outlines": outline, "PageLabels": {"Nums": []}, "Threads": []})
        tags = tags_of(pdf.build())
        self.assertTrue({"ch_outline", "ch_annotation", "ch_redact_annot", "ch_page_labels", "ch_threads",
                         "ch_unreferenced"} <= tags)

    def test_generator_mapping(self):
        cases = {"Microsoft® Word for Microsoft 365": "gen_word", "Microsoft: Print To PDF": "gen_print_to_pdf",
                 "Skia/PDF m120": "gen_chrome", "LibreOffice 7.6": "gen_libreoffice",
                 "macOS Version 14.0 (Build 23A344) Quartz PDFContext": "gen_quartz",
                 "GPL Ghostscript 10.02": "gen_ghostscript", "pdfTeX-1.40.25": "gen_pdftex",
                 "Acrobat Distiller 23.0": "gen_distiller", "Adobe Acrobat Pro 23": "gen_acrobat",
                 "iText® 7.2.5": "gen_itext", "ReportLab PDF Library": "gen_reportlab",
                 "Something Else 1.0": "gen_other"}
        for producer, tag in cases.items():
            with self.subTest(producer):
                self.assertEqual(autotag.generator_tag(producer), tag)
        self.assertIsNone(autotag.generator_tag(""))


if __name__ == "__main__":
    unittest.main()
