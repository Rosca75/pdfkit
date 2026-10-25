// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests that a write is a function of its input and plan: one plan written
// twice, on one handle or on two, gives the same bytes and the same /ID, and
// the report names the output's own hash.

#include "pdfkit/pdfkit.h"

#include "platform/byte_span.hpp"
#include "platform/crypto/sha2.hpp"
#include "platform/json_reader.hpp"
#include "support/pdf_builder_channels.hpp"
#include "writer/output_scan.hpp"

#include <doctest.h>

#include <cstdint>
#include <string>

namespace {

using pdfkit::test::PdfBuilderChannels;

std::string hex(const std::vector<std::byte>& bytes) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string out;
  for (const std::byte value : bytes) {
    const auto byte = std::to_integer<unsigned>(value);
    out += digits[byte >> 4U];
    out += digits[byte & 0x0FU];
  }
  return out;
}

}  // namespace

TEST_CASE("one plan written twice gives the same bytes and the same /ID") {
  const std::string source = PdfBuilderChannels::leak_fixture(4);
  const std::string first = pdfkit::test::identity_write(source);
  const std::string second = pdfkit::test::identity_write(source);
  CHECK(first == second);
  CHECK(pdfkit::test::select_write(source, {3, 1}) == pdfkit::test::select_write(source, {3, 1}));

  // Twice on one handle: the write leaves the document as it found it.
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_document* doc = nullptr;
  REQUIRE(pdfkit_open(reinterpret_cast<const std::uint8_t*>(source.data()), source.size(),
                      nullptr, &doc, &out) == PDFKIT_OK);
  pdfkit_buffer_free(&out);
  pdfkit_edit* edit = nullptr;
  REQUIRE(pdfkit_edit_new(doc, &edit, &out) == PDFKIT_OK);
  pdfkit_buffer_free(&out);
  std::string outputs[2];
  for (std::string& output : outputs) {
    pdfkit_buffer pdf{nullptr, 0, nullptr};
    REQUIRE(pdfkit_write(edit, nullptr, &pdf, &out) == PDFKIT_OK);
    output.assign(reinterpret_cast<const char*>(pdf.data), pdf.size);
    pdfkit_buffer_free(&pdf);
    pdfkit_buffer_free(&out);
  }
  pdfkit_edit_free(edit);
  pdfkit_close(doc);
  CHECK(outputs[0] == outputs[1]);
  CHECK(outputs[0] == first);

  const std::size_t at = first.find("/ID [<");
  REQUIRE(at != std::string::npos);
  const auto report = pdfkit::platform::JsonReader::parse(pdfkit::test::write_report(source));
  CHECK(report.find("sha256")->as_string() ==
        hex(pdfkit::platform::crypto::Sha2::digest(pdfkit::platform::crypto::Sha2::Variant::sha256,
                                                   pdfkit::platform::as_bytes(first))));
  CHECK(report.find("bytes")->as_int64() == static_cast<std::int64_t>(first.size()));
}
