// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Document is one opened PDF: the bytes (copied), the header found within the
// first 1,024 bytes (every later offset taken relative to it, as PDFium, QPDF
// and pdf.js take it; read in the design record), the cross-reference table
// read along the trailer chain or rebuilt by reconstruction whenever that path
// disappoints, the security handler, the cached resolution of references, the
// decoding of streams through the security handler, the filters and the
// budget, and the pages. Every recovery is recorded once as a repair reason
// and every warning of docs/DESIGN.md chapter 19 the layer meets is recorded
// with its page where it has one. The document reads nothing outside the bytes
// it was given: it follows no action, opens no embedded file and no external
// stream (/F), and resolves no reference outside its own table.

#ifndef PDFKIT_SYNTAX_DOCUMENT_HPP
#define PDFKIT_SYNTAX_DOCUMENT_HPP

#include "platform/budget.hpp"
#include "platform/byte_span.hpp"
#include "syntax/cross_reference.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/filters.hpp"
#include "syntax/limits.hpp"
#include "syntax/object.hpp"
#include "syntax/object_streams.hpp"
#include "syntax/page.hpp"
#include "syntax/security_handler.hpp"
#include "syntax/stream.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::syntax {

class Document {
 public:
  // A warning of docs/DESIGN.md chapter 19.
  struct Warning {
    std::string code;
    std::string message;
    // The page it concerns, 1-based, when it has one.
    std::optional<std::uint32_t> page;
    friend bool operator==(const Warning&, const Warning&) = default;
  };

  // Repair reasons past this count are summarised in one line.
  static constexpr std::size_t max_repairs = 100;

  // Opens bytes under limits. Throws platform::Error with a refusal code of
  // docs/DESIGN.md chapter 19 when the bytes cannot be opened.
  [[nodiscard]] static std::unique_ptr<Document> open(platform::ByteSpan bytes,
                                                      const Limits& limits);

  Document(const Document&) = delete;
  Document& operator=(const Document&) = delete;
  Document(Document&&) = delete;
  Document& operator=(Document&&) = delete;
  ~Document() = default;

  // "1.7": the header's version, raised by a later catalog /Version.
  [[nodiscard]] const std::string& version() const noexcept { return version_; }
  // Where %PDF- was found, or no value when it was not in the first 1,024 bytes.
  [[nodiscard]] std::optional<std::size_t> header_offset() const noexcept { return header_; }
  // The bytes from the header on; every offset of the file is relative to them.
  [[nodiscard]] platform::ByteSpan bytes() const noexcept { return file_; }

  [[nodiscard]] const Dictionary& trailer() const noexcept { return trailer_; }
  [[nodiscard]] const std::map<std::uint32_t, CrossReference::Entry>& entries() const noexcept {
    return entries_;
  }
  // Whether the table was rebuilt by reconstruction.
  [[nodiscard]] bool reconstructed() const noexcept { return reconstructed_; }

  // The object behind a reference; null when it is missing or its resolution
  // is already under way (a cycle). Strings come back decrypted.
  [[nodiscard]] Object object(Object::Reference reference);
  // object follows references (a chain of at most 32); any other object is
  // returned as it is.
  [[nodiscard]] Object resolve(const Object& object);
  // The resolved value of key in dictionary.
  [[nodiscard]] Object resolve_entry(const Dictionary& dictionary, std::string_view key);

  [[nodiscard]] Object catalog();
  [[nodiscard]] Object info();

  // The decoded data of a stream: decrypted (unless created), then through its filters as far
  // as the first image codec. A stream with an external file (/F) decodes to
  // nothing. A damaged stream is the warning damaged_stream, naming page when
  // given.
  [[nodiscard]] Filters::Result decode(const Stream& stream,
                                       std::optional<std::uint32_t> page = std::nullopt);
  // The raw (still encrypted and encoded) bytes of a stream; a created
  // stream's own bytes, which are never encrypted.
  [[nodiscard]] platform::ByteSpan raw(const Stream& stream) const noexcept;
  // The decoded pieces of a /Contents value (a stream or an array of them),
  // in order; a piece that is not a stream is skipped.
  [[nodiscard]] std::vector<std::vector<std::byte>> content_pieces(const Object& contents,
                                                                   std::uint32_t page);

  [[nodiscard]] const std::vector<Page>& pages() const noexcept { return pages_; }
  [[nodiscard]] const std::optional<SecurityHandler>& security() const noexcept {
    return security_;
  }

  [[nodiscard]] const std::vector<std::string>& repairs() const noexcept { return repairs_; }
  [[nodiscard]] const std::vector<Warning>& warnings() const noexcept { return warnings_; }
  // Records a repair reason once.
  void repair(std::string reason);
  // Records a warning once.
  void warn(std::string code, std::string message, std::optional<std::uint32_t> page);

  [[nodiscard]] platform::Budget& budget() noexcept { return budget_; }
  [[nodiscard]] const Limits& limits() const noexcept { return limits_; }

 private:
  Document(platform::ByteSpan bytes, const Limits& limits);

  void load();
  // The reason the table read along the trailer chain cannot be used, or
  // empty when it can.
  [[nodiscard]] std::string check_table();
  void reconstruct(const std::string& reason);
  void setup_security();
  void parse_all();
  void build_pages();
  [[nodiscard]] std::string linearization_fault();
  [[nodiscard]] const ObjectStreams::Contents* object_stream(std::uint32_t number);
  [[nodiscard]] Object decrypt_strings(const Object& object, Object::Reference owner) const;
  // Parses the object at an offset entry without caching or decrypting it.
  [[nodiscard]] Object parse_raw(std::uint32_t number);
  [[noreturn]] void refuse_no_page() const;

  std::vector<std::byte> storage_;
  platform::ByteSpan file_;
  std::optional<std::size_t> header_;
  std::string version_;
  Limits limits_;
  platform::Budget budget_;
  std::map<std::uint32_t, CrossReference::Entry> entries_;
  Dictionary trailer_;
  bool reconstructed_ = false;
  std::optional<SecurityHandler> security_;
  std::optional<std::uint32_t> encrypt_number_;
  std::map<std::uint32_t, Object> cache_;
  std::set<std::uint32_t> in_progress_;
  ObjectStreams object_streams_;
  int object_stream_depth_ = 0;
  std::vector<Page> pages_;
  std::vector<std::string> repairs_;
  std::set<std::string> repair_set_;
  std::size_t repairs_dropped_ = 0;
  std::vector<Warning> warnings_;
};

}  // namespace pdfkit::syntax

#endif  // PDFKIT_SYNTAX_DOCUMENT_HPP
