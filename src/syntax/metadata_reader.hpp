// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// MetadataReader reads a document's metadata, docs/DESIGN.md section 10: the
// eight fields of the Info dictionary (ISO 32000-1 section 14.3.3, read) and
// the XMP properties they pair with in the catalog's /Metadata packet
// (section 14.3.2), side by side, one row per field, with whether the two
// statements agree, because a viewer shows whichever it prefers and a file
// whose two statements disagree says two things. Info strings are read
// through PdfDocEncoding (a UTF-16LE string with its mark is read and
// counted); an encrypted document's strings come back decrypted, and a packet
// left unencrypted by /EncryptMetadata false is read as it is, both by the
// document. Dates are kept as written; the agreement test compares them
// normalised to ISO 8601 and never rewrites them. A packet that does not scan
// is reported with the reason and the warning xmp_unreadable, never guessed
// at.

#ifndef PDFKIT_SYNTAX_METADATA_READER_HPP
#define PDFKIT_SYNTAX_METADATA_READER_HPP

#include "syntax/document.hpp"
#include "syntax/pdf_doc_encoding.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::syntax {

class MetadataReader {
 public:
  enum class Field : std::uint8_t {
    title,
    author,
    subject,
    keywords,
    creator,
    producer,
    creation_date,
    mod_date
  };

  static constexpr std::array<Field, 8> fields = {
      Field::title,   Field::author,   Field::subject,       Field::keywords,
      Field::creator, Field::producer, Field::creation_date, Field::mod_date};

  // An XMP property a field pairs with: its namespace, its local name and
  // the name it is reported under.
  struct Pair {
    std::string_view ns;
    std::string_view local;
    std::string_view name;
  };

  // One value of the XMP packet for a row.
  struct XmpValue {
    // "dc:title" and so on, whatever prefix the packet writes.
    std::string property;
    std::string value;
  };

  struct Row {
    Field field = Field::title;
    std::optional<std::string> info;
    PdfDocEncoding::Form info_form = PdfDocEncoding::Form::pdf_doc;
    // The paired properties present, in the order of pairs(field).
    std::vector<XmpValue> xmp;
    // A date field's values in ISO 8601, when they parse.
    std::optional<std::string> info_iso;
    std::optional<std::string> xmp_iso;
    // Whether the Info value and the first XMP value say the same; no value
    // when either side is missing.
    std::optional<bool> agree;
  };

  struct Result {
    bool info_present = false;
    bool xmp_present = false;
    bool xmp_readable = false;
    std::string xmp_fault;
    std::vector<Row> rows;
    // Info strings written UTF-16LE with a mark.
    std::size_t utf16le = 0;
    // The packet's decoded bytes, when it decoded.
    std::string packet;
  };

  // Reads document's metadata; a packet that does not scan is also recorded
  // as the document's warning xmp_unreadable.
  [[nodiscard]] static Result read(Document& document);

  // "Title", "Author", "Subject", "Keywords", "Creator", "Producer",
  // "CreationDate", "ModDate".
  [[nodiscard]] static std::string_view name(Field field) noexcept;
  [[nodiscard]] static std::optional<Field> field(std::string_view name) noexcept;
  // The properties field pairs with: Title dc:title, Author dc:creator,
  // Subject dc:description, Keywords pdf:Keywords and dc:subject, Creator
  // xmp:CreatorTool, Producer pdf:Producer, CreationDate xmp:CreateDate,
  // ModDate xmp:ModifyDate.
  [[nodiscard]] static std::span<const Pair> pairs(Field field) noexcept;
  [[nodiscard]] static bool is_date(Field field) noexcept;

  // A PDF date (ISO 32000-1 section 7.9.4, read: D:YYYYMMDDHHmmSSOHH'mm',
  // every part after the year optional) in ISO 8601, or no value.
  [[nodiscard]] static std::optional<std::string> pdf_date_to_iso(std::string_view text);
  // An ISO 8601 date as XMP writes it (YYYY[-MM[-DD[Thh:mm[:ss[.s]]TZD]]],
  // recalled from the XMP specification part 1) normalised the same way, or
  // no value.
  [[nodiscard]] static std::optional<std::string> iso_date(std::string_view text);
  // An ISO 8601 date as a PDF date, or no value.
  [[nodiscard]] static std::optional<std::string> iso_to_pdf_date(std::string_view text);
};

}  // namespace pdfkit::syntax

#endif  // PDFKIT_SYNTAX_METADATA_READER_HPP
