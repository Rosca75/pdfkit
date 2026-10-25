// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// PdfBuilder is the fixture writer of the unit tier: no PDF is committed
// (CLAUDE.md section 7.2), so every test writes the file it reads here. It
// writes objects and streams (raw or Flate, with a /Length that may lie by a
// given amount), a classic table with 19-, 20- or 21-byte entries or a
// cross-reference stream with any /W, object streams, incremental updates
// with /Prev, a hybrid /XRefStm, a linearization dictionary, and encryption
// under each revision through PdfEncryptor. Every damage the dataset's
// mutator knows is a named knob of Layout, so a test reads as the file it
// makes. Strings that must be encrypted with their object are written through
// str(), which the build replaces by the hexadecimal string, encrypted when
// the file is.

#ifndef PDFKIT_TESTS_SUPPORT_PDF_BUILDER_HPP
#define PDFKIT_TESTS_SUPPORT_PDF_BUILDER_HPP

#include "support/pdf_encryptor.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::test {

// The form of the cross-reference sections the build writes.
enum class XrefForm : std::uint8_t { table, stream, hybrid };

// How the build writes one stream.
struct StreamOptions {
  // Compress the data with zlib and add /Filter /FlateDecode.
  bool flate = false;
  // The /Length written is the true length plus this.
  std::int64_t length_delta = 0;
  // /Length is a reference to an object holding it.
  bool length_indirect = false;
  // The keyword endstream is left out.
  bool omit_endstream = false;
};

// How the build writes one object.
struct ObjectOptions {
  std::uint32_t generation = 0;
  // The keyword endobj is left out.
  bool omit_endobj = false;
  // The object is written inside an object stream (cross-reference stream or
  // hybrid layouts only).
  bool in_object_stream = false;
};

// The file's layout and its damage knobs, each named after the mutation it
// reproduces.
struct Layout {
  std::string version = "1.7";
  XrefForm xref = XrefForm::table;
  // 19, 20 or 21.
  int entry_bytes = 20;
  // Compress the cross-reference stream with Flate and the PNG Up predictor.
  bool xref_stream_predictor = false;
  // The /W of the cross-reference stream; zero widths are computed.
  std::array<int, 3> xref_widths{0, 0, 0};
  std::string junk_before_header;
  bool omit_eof = false;
  // The value written after startxref instead of the true offset.
  std::optional<std::int64_t> startxref;
  // Added to every in-use offset of the newest table.
  std::int64_t offset_shift = 0;
  // The newest table's xref keyword and entries are left out; its trailer stays.
  bool omit_table = false;
  bool omit_root = false;
  // The /Prev written in the newest section instead of the true offset.
  std::optional<std::int64_t> prev;
  // The newest trailer and startxref are written twice.
  bool duplicate_trailer = false;
  // The first subsection of the first table is labelled 1 instead of 0.
  bool table_off_by_one = false;
  // Write a linearization dictionary as the first object, with /O naming the
  // first page's object number plus this.
  std::optional<std::int64_t> linearized_first_page_delta;
};

class PdfBuilder {
 public:
  using XrefForm = test::XrefForm;
  using StreamOptions = test::StreamOptions;
  using ObjectOptions = test::ObjectOptions;
  using Layout = test::Layout;

  PdfBuilder();

  // A string the build writes in hexadecimal, encrypted when the file is.
  [[nodiscard]] static std::string str(std::string_view bytes);

  // A number for an object set later.
  std::uint32_t reserve();
  // Adds an object in the current revision; returns its number.
  std::uint32_t add(std::string body, ObjectOptions options = {});
  std::uint32_t add_stream(std::string entries, std::string data, StreamOptions options = {},
                           ObjectOptions object = {});
  // Sets object number in the current revision.
  void set(std::uint32_t number, std::string body, ObjectOptions options = {});
  void set_stream(std::uint32_t number, std::string entries, std::string data,
                  StreamOptions options = {}, ObjectOptions object = {});

  void set_root(std::uint32_t number) { root_ = number; }
  void set_info(std::uint32_t number) { info_ = number; }
  // Entries appended to the current revision's trailer.
  void add_trailer_entries(const std::string& entries);

  // Starts an incremental update: later objects go into a new revision.
  void new_revision();

  // Encrypts the file with the standard security handler.
  void encrypt(PdfEncryptor::Spec spec);

  // A document skeleton: a catalog and a page tree whose /Kids the build
  // writes from the pages added with add_page.
  void begin_document();
  // Adds a page drawing content (a Flate stream when flate) with extra page
  // entries; returns the page object's number.
  std::uint32_t add_page(const std::string& content, const std::string& extra_entries = "",
                         bool flate = false);
  [[nodiscard]] std::uint32_t catalog() const noexcept { return root_; }
  [[nodiscard]] std::uint32_t page_tree() const noexcept { return pages_; }
  [[nodiscard]] const std::vector<std::uint32_t>& pages() const noexcept { return page_list_; }

  [[nodiscard]] std::string build(const Layout& layout) const;
  [[nodiscard]] std::string build() const { return build(Layout{}); }

  // The deflated form of data, as zlib writes it.
  [[nodiscard]] static std::string deflate(const std::string& data);

 private:
  struct Entry {
    std::string body;
    bool is_stream = false;
    std::string entries;
    std::string data;
    StreamOptions stream;
    ObjectOptions object;
    std::optional<std::uint32_t> length_object;
  };
  struct Revision {
    std::map<std::uint32_t, Entry> objects;
    std::string trailer_entries;
  };

  void put(std::uint32_t number, Entry entry);

  std::vector<Revision> revisions_;
  std::uint32_t next_number_ = 1;
  std::uint32_t root_ = 0;
  std::uint32_t info_ = 0;
  std::uint32_t pages_ = 0;
  std::vector<std::uint32_t> page_list_;
  std::optional<PdfEncryptor::Spec> encryption_;
  std::uint32_t encrypt_number_ = 0;
};

}  // namespace pdfkit::test

#endif  // PDFKIT_TESTS_SUPPORT_PDF_BUILDER_HPP
