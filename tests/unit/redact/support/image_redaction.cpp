// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The image treatment fixture and the calls through the C ABI. The fixture's
// samples are distinct patterns, so a sample left untreated is told apart
// from the fill, and the shared image's data is Flate-compressed, so its
// encoded bytes are a byte string a leak search can look for.

#include "redact/support/image_redaction.hpp"

#include "platform/byte_span.hpp"
#include "platform/json_reader.hpp"
#include "support/pdf_builder.hpp"
#include "syntax/document.hpp"
#include "syntax/limits.hpp"

#include <doctest.h>

namespace pdfkit::test {

namespace {

std::string take(pdfkit_buffer& buffer) {
  std::string text(reinterpret_cast<const char*>(buffer.data), buffer.size);
  pdfkit_buffer_free(&buffer);
  return text;
}

std::string pattern(std::size_t size, int seed) {
  std::string out(size, '\0');
  for (std::size_t i = 0; i < size; ++i) {
    // A stride of its own per seed, so no two patterns share a run of bytes.
    const std::size_t stride = 29 + (static_cast<std::size_t>(seed) * 6);
    out[i] = static_cast<char>(((i * stride) + static_cast<std::size_t>(seed) * 53 + 17) % 251 + 1);
  }
  return out;
}

}  // namespace

ImageFile image_file() {
  ImageFile file;
  PdfBuilder builder;
  builder.begin_document();
  const std::string shared = pattern(16 * 12 * 3, 1);
  const std::string kept = pattern(8 * 8, 2);
  // Printable bytes, so the inline data holds no white space before an "EI".
  std::string inline_data;
  for (int i = 0; i < 16; ++i) {
    inline_data += static_cast<char>('A' + i);
  }
  const std::string masked = pattern(16 * 16 * 3, 3);
  const std::string mask = pattern(16 * 16, 4);
  file.data["shared"] = PdfBuilder::deflate(shared);
  file.data["kept"] = kept;
  file.data["inline"] = inline_data;
  file.data["masked"] = masked;
  file.data["mask"] = mask;
  const std::uint32_t image = builder.add_stream(
      "/Type /XObject /Subtype /Image /Width 16 /Height 12 /ColorSpace /DeviceRGB "
      "/BitsPerComponent 8",
      shared, StreamOptions{true});
  const std::uint32_t second = builder.add_stream(
      "/Type /XObject /Subtype /Image /Width 8 /Height 8 /ColorSpace /DeviceGray "
      "/BitsPerComponent 8",
      kept);
  const std::uint32_t soft = builder.add_stream(
      "/Type /XObject /Subtype /Image /Width 16 /Height 16 /ColorSpace /DeviceGray "
      "/BitsPerComponent 8",
      mask);
  const std::uint32_t with_mask = builder.add_stream(
      "/Type /XObject /Subtype /Image /Width 16 /Height 16 /ColorSpace /DeviceRGB "
      "/BitsPerComponent 8 /SMask " +
          std::to_string(soft) + " 0 R",
      masked);
  const std::uint32_t font = builder.add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
  const std::uint32_t form = builder.add_stream(
      "/Type /XObject /Subtype /Form /BBox [0 0 600 800] /Resources << /XObject << /ImA " +
          std::to_string(image) + " 0 R >> >>",
      "q 120 0 0 90 30 30 cm /ImA Do Q");
  const std::string ref_image = std::to_string(image) + " 0 R";
  builder.add_page(
      "q 100 0 0 80 50 600 cm /ImA Do Q "
      "q 40 0 0 40 300 600 cm BI /W 4 /H 4 /CS /G /BPC 8 ID " +
          inline_data +
          " EI Q "
          "BT /F1 12 Tf 50 500 Td (Hello images) Tj ET "
          "q 60 0 0 60 400 400 cm /ImB Do Q",
      "/MediaBox [0 0 612 792] /Resources << /XObject << /ImA " + ref_image + " /ImB " +
          std::to_string(second) + " 0 R >> /Font << /F1 " + std::to_string(font) + " 0 R >> >>");
  builder.add_page("q 200 0 0 160 100 100 cm /ImA Do Q q 64 0 0 64 400 500 cm /ImM Do Q",
                   "/MediaBox [0 0 612 792] /Resources << /XObject << /ImA " + ref_image +
                       " /ImM " + std::to_string(with_mask) + " 0 R >> >>");
  builder.add_page("q 1 0 0 1 0 0 cm /Fm1 Do Q",
                   "/MediaBox [0 0 612 792] /Resources << /XObject << /Fm1 " +
                       std::to_string(form) + " 0 R >> >>");
  file.bytes = builder.build();
  const platform::JsonValue listed = list_images(file.bytes);
  for (const platform::JsonValue& entry : listed.find("images")->as_array()) {
    const std::string& identity = entry.find("identity")->as_string();
    const platform::JsonValue* object = entry.find("object");
    const std::int64_t width = entry.find("width")->as_int64().value_or(0);
    const std::int64_t height = entry.find("height")->as_int64().value_or(0);
    if (object->kind() == platform::JsonValue::Kind::null) {
      file.identity["inline"] = identity;
    } else if (width == 16 && height == 12) {
      file.identity["shared"] = identity;
    } else if (width == 8) {
      file.identity["kept"] = identity;
    } else {
      file.identity["masked"] = identity;
    }
  }
  return file;
}

ImageRedaction treat(const std::string& file, const std::string& treatments_json,
                     const std::string& text_json, const std::vector<std::uint32_t>& pages) {
  ImageRedaction result;
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_document* doc = nullptr;
  pdfkit_status status = pdfkit_open(reinterpret_cast<const std::uint8_t*>(file.data()),
                                     file.size(), nullptr, &doc, &out);
  (void)take(out);
  REQUIRE(status == PDFKIT_OK);
  pdfkit_edit* edit = nullptr;
  status = pdfkit_edit_new(doc, &edit, &out);
  (void)take(out);
  REQUIRE(status == PDFKIT_OK);
  if (!text_json.empty()) {
    status = pdfkit_redact_text(edit, text_json.c_str(), &out);
    const std::string answer = take(out);
    INFO(answer);
    REQUIRE(status == PDFKIT_OK);
  }
  result.treatment_status = pdfkit_redact_images(edit, treatments_json.c_str(), &out);
  result.answer = take(out);
  if (result.treatment_status == PDFKIT_OK && !pages.empty()) {
    std::string json = "{\"pages\": [";
    for (std::size_t i = 0; i < pages.size(); ++i) {
      json += (i == 0 ? "" : ", ") + std::to_string(pages[i]);
    }
    json += "]}";
    status = pdfkit_select_pages(edit, json.c_str(), &out);
    (void)take(out);
    REQUIRE(status == PDFKIT_OK);
  }
  if (result.treatment_status == PDFKIT_OK) {
    pdfkit_buffer pdf{nullptr, 0, nullptr};
    result.write_status = pdfkit_write(edit, nullptr, &pdf, &out);
    result.report = take(out);
    result.pdf = take(pdf);
  }
  pdfkit_edit_free(edit);
  pdfkit_close(doc);
  return result;
}

platform::JsonValue list_images(const std::string& bytes) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_document* doc = nullptr;
  pdfkit_status status = pdfkit_open(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                     bytes.size(), nullptr, &doc, &out);
  (void)take(out);
  REQUIRE(status == PDFKIT_OK);
  status = pdfkit_images(doc, nullptr, &out);
  const std::string listed = take(out);
  pdfkit_close(doc);
  INFO(listed);
  REQUIRE(status == PDFKIT_OK);
  return platform::JsonReader::parse(listed);
}

std::map<std::uint32_t, images::ImageDecoder::Samples> samples_of(const std::string& output,
                                                                  std::uint32_t width,
                                                                  std::uint32_t height) {
  std::map<std::uint32_t, images::ImageDecoder::Samples> out;
  auto document = syntax::Document::open(platform::as_bytes(output), syntax::Limits{});
  for (const auto& [number, unused] : document->entries()) {
    const syntax::Object value = document->object({number, 0});
    const syntax::Stream* stream = value.stream();
    if (stream == nullptr) {
      continue;
    }
    const syntax::Dictionary& dictionary = stream->dictionary();
    const syntax::Object* subtype = dictionary.find("Subtype");
    if (subtype == nullptr || subtype->name() == nullptr || *subtype->name() != "Image" ||
        dictionary.get("Width").integer() != width ||
        dictionary.get("Height").integer() != height) {
      continue;
    }
    images::ImageDecoder::SamplesResult read =
        images::ImageDecoder::samples(*document, *stream, false);
    REQUIRE(read.samples);
    out.emplace(number, std::move(*read.samples));
  }
  return out;
}

std::string contents_of(const std::string& output) {
  auto document = syntax::Document::open(platform::as_bytes(output), syntax::Limits{});
  std::string out;
  for (const syntax::Page& page : document->pages()) {
    for (const std::vector<std::byte>& piece :
         document->content_pieces(page.contents, page.number)) {
      out.append(reinterpret_cast<const char*>(piece.data()), piece.size());
      out += '\n';
    }
  }
  for (const auto& [number, unused] : document->entries()) {
    const syntax::Object value = document->object({number, 0});
    const syntax::Stream* stream = value.stream();
    const syntax::Object* subtype =
        stream != nullptr ? stream->dictionary().find("Subtype") : nullptr;
    if (subtype != nullptr && subtype->name() != nullptr && *subtype->name() == "Form") {
      const std::vector<std::byte> bytes = document->decode(*stream).bytes;
      out.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      out += '\n';
    }
  }
  return out;
}

}  // namespace pdfkit::test
