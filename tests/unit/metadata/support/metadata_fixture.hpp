// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The fixtures the metadata tests share: an XMP packet written around a
// description's attributes and body, and a one-page document holding an
// Info dictionary of the given entries and, when given, that packet as the
// catalog's /Metadata, so each test reads as the file it makes.

#ifndef PDFKIT_TESTS_METADATA_SUPPORT_METADATA_FIXTURE_HPP
#define PDFKIT_TESTS_METADATA_SUPPORT_METADATA_FIXTURE_HPP

#include "pdfkit/pdfkit.h"

#include "support/pdf_encryptor.hpp"

#include <optional>
#include <string>

namespace pdfkit::test {

struct MetadataFixture {
  // The Info dictionary's entries, written as they are ("/Title (x)"); no
  // Info dictionary when empty.
  std::string info;
  // The packet; no /Metadata when empty.
  std::string packet;
  // Encrypts the file when given.
  std::optional<PdfEncryptor::Spec> encryption;
  // The page's content.
  std::string content = "BT /F1 12 Tf 72 700 Td (Body text) Tj ET";

  [[nodiscard]] std::string build() const;

  // A write through the C ABI: the statuses and answers of
  // pdfkit_set_metadata (when fields is not empty), pdfkit_redact_text (when
  // matches is not empty) and pdfkit_write, and the output.
  struct Write {
    pdfkit_status set_status = PDFKIT_OK;
    std::string set_answer;
    pdfkit_status write_status = PDFKIT_OK;
    std::string report;
    std::string pdf;
  };
  [[nodiscard]] static Write write(const std::string& file, const std::string& fields,
                                   const std::string& matches = "");

  // A packet: the xpacket wrapper, x:xmpmeta, rdf:RDF and one
  // rdf:Description with the dc, xmp, pdf and xmpMM namespaces declared,
  // attributes added to its start tag and body inside it.
  [[nodiscard]] static std::string xmp(const std::string& body, const std::string& attributes = "");
};

}  // namespace pdfkit::test

#endif  // PDFKIT_TESTS_METADATA_SUPPORT_METADATA_FIXTURE_HPP
