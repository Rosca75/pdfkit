// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The fixture writer's build: the file layout of ISO 32000-1 section 7.5
// (header, body, cross-reference table or stream, trailer, startxref, %%EOF)
// written revision by revision, with every damage knob applied where the
// mutation it reproduces applies it.

#include "support/pdf_builder.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace pdfkit::test {

namespace {

constexpr char marker = '\x01';

std::string hex_digits(std::string_view bytes) {
  static constexpr char digits[] = "0123456789ABCDEF";
  std::string out;
  for (const char value : bytes) {
    const auto byte = static_cast<unsigned char>(value);
    out.push_back(digits[byte >> 4U]);
    out.push_back(digits[byte & 0x0FU]);
  }
  return out;
}

std::string unhex(std::string_view digits) {
  std::string out;
  for (std::size_t i = 0; i + 1 < digits.size(); i += 2) {
    out.push_back(static_cast<char>(std::stoi(std::string(digits.substr(i, 2)), nullptr, 16)));
  }
  return out;
}

// Replaces every str() marker of body with its hexadecimal string, encrypted
// for object number/generation when an encryptor is given.
std::string expand_strings(const std::string& body, const PdfEncryptor* encryptor,
                           std::uint32_t number, std::uint32_t generation) {
  std::string out;
  std::size_t at = 0;
  while (true) {
    const std::size_t open = body.find(marker, at);
    if (open == std::string::npos) {
      out.append(body, at, std::string::npos);
      return out;
    }
    const std::size_t close = body.find(marker, open + 1);
    if (close == std::string::npos) {
      throw std::logic_error("PdfBuilder: an unterminated str() marker");
    }
    out.append(body, at, open - at);
    std::string bytes = unhex(std::string_view(body).substr(open + 1, close - open - 1));
    if (encryptor != nullptr) {
      bytes = encryptor->encrypt_string(number, generation, bytes);
    }
    out += "<" + hex_digits(bytes) + ">";
    at = close + 1;
  }
}

std::string padded_number(std::int64_t value, int width) {
  std::string text = std::to_string(value);
  if (std::cmp_less(text.size(), width)) {
    text.insert(0, static_cast<std::size_t>(width) - text.size(), '0');
  }
  return text;
}

std::string table_entry(std::int64_t offset, std::uint32_t generation, bool in_use, int bytes) {
  std::string entry =
      padded_number(offset, 10) + " " + padded_number(generation, 5) + " " + (in_use ? "n" : "f");
  if (bytes == 19) {
    return entry + "\n";
  }
  if (bytes == 21) {
    return entry + " \r\n";
  }
  return entry + "\r\n";
}

struct XrefRow {
  std::uint32_t number;
  int type;  // 0 free, 1 offset, 2 compressed
  std::uint64_t field2;
  std::uint64_t field3;
};

int width_for(std::uint64_t value) {
  int width = 1;
  while (width < 8 && (value >> (8U * static_cast<unsigned>(width))) != 0) {
    ++width;
  }
  return width;
}

void append_be(std::string& out, std::uint64_t value, int width) {
  for (int i = width - 1; i >= 0; --i) {
    out.push_back(static_cast<char>((value >> (8U * static_cast<unsigned>(i))) & 0xFFU));
  }
}

// Consecutive runs of numbers, as subsection pairs.
std::vector<std::pair<std::uint32_t, std::uint32_t>> runs(const std::vector<XrefRow>& rows) {
  std::vector<std::pair<std::uint32_t, std::uint32_t>> out;
  for (const XrefRow& row : rows) {
    if (!out.empty() && out.back().first + out.back().second == row.number) {
      ++out.back().second;
    } else {
      out.emplace_back(row.number, 1);
    }
  }
  return out;
}

}  // namespace

PdfBuilder::PdfBuilder() : revisions_(1) {}

std::string PdfBuilder::str(std::string_view bytes) {
  return std::string(1, marker) + hex_digits(bytes) + std::string(1, marker);
}

std::string PdfBuilder::deflate(const std::string& data) {
  // A zlib stream (RFC 1950) at the default level, through zlib's deflate.
  z_stream stream{};
  if (deflateInit(&stream, Z_DEFAULT_COMPRESSION) != Z_OK) {
    throw std::runtime_error("PdfBuilder: zlib deflateInit failed");
  }
  std::string out(deflateBound(&stream, static_cast<uLong>(data.size())), '\0');
  std::string input = data;
  stream.next_in = reinterpret_cast<Bytef*>(input.data());
  stream.avail_in = static_cast<uInt>(input.size());
  stream.next_out = reinterpret_cast<Bytef*>(out.data());
  stream.avail_out = static_cast<uInt>(out.size());
  const int result = ::deflate(&stream, Z_FINISH);
  out.resize(stream.total_out);
  deflateEnd(&stream);
  if (result != Z_STREAM_END) {
    throw std::runtime_error("PdfBuilder: zlib deflate failed");
  }
  return out;
}

std::uint32_t PdfBuilder::reserve() {
  return next_number_++;
}

void PdfBuilder::put(std::uint32_t number, Entry entry) {
  next_number_ = std::max(next_number_, number + 1);
  revisions_.back().objects[number] = std::move(entry);
}

std::uint32_t PdfBuilder::add(std::string body, ObjectOptions options) {
  const std::uint32_t number = reserve();
  set(number, std::move(body), options);
  return number;
}

std::uint32_t PdfBuilder::add_stream(std::string entries, std::string data, StreamOptions options,
                                     ObjectOptions object) {
  const std::uint32_t number = reserve();
  set_stream(number, std::move(entries), std::move(data), options, object);
  return number;
}

void PdfBuilder::set(std::uint32_t number, std::string body, ObjectOptions options) {
  Entry entry;
  entry.body = std::move(body);
  entry.object = options;
  put(number, std::move(entry));
}

void PdfBuilder::set_stream(std::uint32_t number, std::string entries, std::string data,
                            StreamOptions options, ObjectOptions object) {
  Entry entry;
  entry.is_stream = true;
  entry.entries = std::move(entries);
  entry.data = std::move(data);
  entry.stream = options;
  entry.object = object;
  if (options.length_indirect) {
    entry.length_object = reserve();
  }
  put(number, std::move(entry));
}

void PdfBuilder::add_trailer_entries(const std::string& entries) {
  revisions_.back().trailer_entries += " " + entries;
}

void PdfBuilder::new_revision() {
  revisions_.emplace_back();
}

void PdfBuilder::encrypt(PdfEncryptor::Spec spec) {
  encryption_ = std::move(spec);
  encrypt_number_ = reserve();
}

void PdfBuilder::begin_document() {
  root_ = reserve();
  pages_ = reserve();
  set(root_, "<< /Type /Catalog /Pages " + std::to_string(pages_) + " 0 R >>");
}

std::uint32_t PdfBuilder::add_page(const std::string& content, const std::string& extra_entries,
                                   bool flate) {
  const std::uint32_t contents = add_stream("", content, StreamOptions{flate, 0, false, false});
  const std::uint32_t page = add("<< /Type /Page /Parent " + std::to_string(pages_) +
                                 " 0 R /MediaBox [0 0 612 792] /Contents " +
                                 std::to_string(contents) + " 0 R " + extra_entries + " >>");
  page_list_.push_back(page);
  return page;
}

std::string PdfBuilder::build(const Layout& layout) const {
  const std::string id0 = "pdfkit-fixture-0";
  std::optional<PdfEncryptor> encryptor;
  if (encryption_) {
    encryptor.emplace(*encryption_, id0);
  }
  const PdfEncryptor* cipher = encryptor ? &*encryptor : nullptr;

  std::vector<Revision> revisions = revisions_;
  std::uint32_t next = next_number_;
  if (pages_ != 0 && !revisions.front().objects.contains(pages_)) {
    std::string kids;
    for (const std::uint32_t page : page_list_) {
      kids += std::to_string(page) + " 0 R ";
    }
    Entry tree;
    tree.body =
        "<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(page_list_.size()) + " >>";
    revisions.front().objects[pages_] = tree;
  }
  if (encryptor) {
    Entry dictionary;
    dictionary.body = encryptor->dictionary();
    revisions.front().objects[encrypt_number_] = dictionary;
  }

  std::string out = layout.junk_before_header;
  const std::size_t header_at = out.size();
  out += "%PDF-" + layout.version + "\n%\xE2\xE3\xCF\xD3\n";
  const auto offset_now = [&]() {
    return static_cast<std::int64_t>(out.size() - header_at);
  };

  std::optional<std::size_t> length_patch;
  std::uint32_t linearization = 0;
  std::int64_t linearization_offset = 0;
  if (layout.linearized_first_page_delta) {
    linearization = next++;
    linearization_offset = offset_now();
    const std::int64_t first_page =
        page_list_.empty() ? 0 : static_cast<std::int64_t>(page_list_.front());
    out += std::to_string(linearization) + " 0 obj\n<< /Linearized 1 /L ";
    length_patch = out.size();
    out += "0000000000 /O " + std::to_string(first_page + *layout.linearized_first_page_delta) +
           " /N " + std::to_string(page_list_.size()) + " /H [0 0] /E 0 /T 0 >>\nendobj\n";
  }

  std::int64_t previous_xref = -1;
  for (std::size_t r = 0; r < revisions.size(); ++r) {
    const Revision& revision = revisions[r];
    const bool newest = r + 1 == revisions.size();
    std::vector<XrefRow> rows;
    if (r == 0) {
      rows.push_back({0, 0, 0, 65535});
      if (linearization != 0) {
        rows.push_back({linearization, 1, static_cast<std::uint64_t>(linearization_offset), 0});
      }
    }
    std::vector<std::pair<std::uint32_t, const Entry*>> packed;
    for (const auto& [number, entry] : revision.objects) {
      if (entry.object.in_object_stream) {
        packed.emplace_back(number, &entry);
        continue;
      }
      const std::uint32_t generation = entry.object.generation;
      const bool is_encrypt_dictionary = encryptor && number == encrypt_number_ && r == 0;
      const PdfEncryptor* strings = is_encrypt_dictionary ? nullptr : cipher;
      if (entry.length_object) {
        // Written before its stream, as the reference resolves either way.
        std::string data = entry.stream.flate ? deflate(entry.data) : entry.data;
        if (cipher != nullptr) {
          data = cipher->encrypt_stream(number, generation, data,
                                        entry.entries.find("/Metadata") != std::string::npos);
        }
        rows.push_back({*entry.length_object, 1, static_cast<std::uint64_t>(offset_now()), 0});
        out += std::to_string(*entry.length_object) + " 0 obj\n" +
               std::to_string(static_cast<std::int64_t>(data.size()) + entry.stream.length_delta) +
               "\nendobj\n";
      }
      rows.push_back({number, 1, static_cast<std::uint64_t>(offset_now()), generation});
      out += std::to_string(number) + " " + std::to_string(generation) + " obj\n";
      if (entry.is_stream) {
        std::string data = entry.stream.flate ? deflate(entry.data) : entry.data;
        if (cipher != nullptr) {
          data = cipher->encrypt_stream(number, generation, data,
                                        entry.entries.find("/Metadata") != std::string::npos);
        }
        std::string length = entry.length_object
                                 ? std::to_string(*entry.length_object) + " 0 R"
                                 : std::to_string(static_cast<std::int64_t>(data.size()) +
                                                  entry.stream.length_delta);
        out += "<< " + expand_strings(entry.entries, strings, number, generation) + " /Length " +
               length + (entry.stream.flate ? " /Filter /FlateDecode" : "") + " >>\nstream\n";
        out += data;
        out += entry.stream.omit_endstream ? "\n" : "\nendstream\n";
      } else {
        out += expand_strings(entry.body, strings, number, generation) + "\n";
      }
      if (!entry.object.omit_endobj) {
        out += "endobj\n";
      }
    }
    if (!packed.empty()) {
      if (layout.xref == XrefForm::table) {
        throw std::logic_error("PdfBuilder: an object stream needs a cross-reference stream");
      }
      const std::uint32_t stream_number = next++;
      std::string header;
      std::string bodies;
      std::uint64_t index = 0;
      for (const auto& [number, entry] : packed) {
        header += std::to_string(number) + " " + std::to_string(bodies.size()) + " ";
        bodies += expand_strings(entry->body, nullptr, number, 0) + "\n";
        rows.push_back({number, 2, stream_number, index++});
      }
      std::string data = header + bodies;
      if (cipher != nullptr) {
        data = cipher->encrypt_stream(stream_number, 0, data, false);
      }
      rows.push_back({stream_number, 1, static_cast<std::uint64_t>(offset_now()), 0});
      out += std::to_string(stream_number) + " 0 obj\n<< /Type /ObjStm /N " +
             std::to_string(packed.size()) + " /First " + std::to_string(header.size()) +
             " /Length " + std::to_string(data.size()) + " >>\nstream\n" + data +
             "\nendstream\nendobj\n";
    }
    std::ranges::sort(rows, [](const XrefRow& a, const XrefRow& b) { return a.number < b.number; });
    if (newest && layout.offset_shift != 0) {
      for (XrefRow& row : rows) {
        if (row.type == 1 && row.field2 != 0) {
          row.field2 = static_cast<std::uint64_t>(static_cast<std::int64_t>(row.field2) +
                                                  layout.offset_shift);
        }
      }
    }

    std::string trailer_entries = " /Size " + std::to_string(next);
    if (root_ != 0 && !layout.omit_root) {
      trailer_entries += " /Root " + std::to_string(root_) + " 0 R";
    }
    if (info_ != 0) {
      trailer_entries += " /Info " + std::to_string(info_) + " 0 R";
    }
    if (encryptor) {
      trailer_entries += " /Encrypt " + std::to_string(encrypt_number_) + " 0 R";
    }
    trailer_entries += " /ID [<" + hex_digits(id0) + "> <" + hex_digits(id0) + ">]";
    if (previous_xref >= 0 || (newest && layout.prev)) {
      trailer_entries +=
          " /Prev " + std::to_string(newest && layout.prev ? *layout.prev : previous_xref);
    }
    trailer_entries += revision.trailer_entries;

    // The hybrid layout writes the compressed entries in a stream the table
    // names through /XRefStm; the table keeps the direct objects.
    std::vector<XrefRow> stream_rows;
    if (layout.xref == XrefForm::hybrid) {
      std::vector<XrefRow> direct;
      for (const XrefRow& row : rows) {
        (row.type == 2 ? stream_rows : direct).push_back(row);
      }
      rows = direct;
    }

    std::int64_t xref_at = offset_now();
    const auto write_stream = [&](std::vector<XrefRow> stream_entries,
                                  const std::string& extra) -> std::int64_t {
      const std::uint32_t number = next++;
      const std::int64_t at = offset_now();
      stream_entries.push_back({number, 1, static_cast<std::uint64_t>(at), 0});
      std::ranges::sort(stream_entries,
                        [](const XrefRow& a, const XrefRow& b) { return a.number < b.number; });
      std::array<int, 3> widths = layout.xref_widths;
      std::uint64_t largest2 = 0;
      std::uint64_t largest3 = 0;
      for (const XrefRow& row : stream_entries) {
        largest2 = std::max(largest2, row.field2);
        largest3 = std::max(largest3, row.field3);
      }
      if (widths[0] == 0 && widths[1] == 0 && widths[2] == 0) {
        widths = {1, width_for(largest2), width_for(largest3)};
      }
      std::string data;
      const int row_bytes = widths[0] + widths[1] + widths[2];
      std::string previous_row(static_cast<std::size_t>(row_bytes), '\0');
      for (const XrefRow& row : stream_entries) {
        std::string bytes;
        if (widths[0] > 0) {
          append_be(bytes, static_cast<std::uint64_t>(row.type), widths[0]);
        }
        append_be(bytes, row.field2, widths[1]);
        append_be(bytes, row.field3, widths[2]);
        if (layout.xref_stream_predictor) {
          // PNG filter type 2 (Up): each byte minus the byte above it.
          std::string up(1, '\x02');
          for (std::size_t i = 0; i < bytes.size(); ++i) {
            up.push_back(static_cast<char>(static_cast<unsigned char>(bytes[i]) -
                                           static_cast<unsigned char>(previous_row[i])));
          }
          previous_row = bytes;
          bytes = up;
        }
        data += bytes;
      }
      std::string index;
      for (const auto& [first, count] : runs(stream_entries)) {
        index += std::to_string(first) + " " + std::to_string(count) + " ";
      }
      std::string dictionary = "<< /Type /XRef /W [";
      dictionary += std::to_string(widths[0]);
      dictionary += ' ';
      dictionary += std::to_string(widths[1]);
      dictionary += ' ';
      dictionary += std::to_string(widths[2]);
      dictionary += "] /Index [";
      dictionary += index;
      dictionary += ']';
      dictionary += extra;
      if (layout.xref_stream_predictor) {
        data = deflate(data);
        dictionary += " /Filter /FlateDecode /DecodeParms << /Predictor 12 /Columns ";
        dictionary += std::to_string(row_bytes);
        dictionary += " >>";
      }
      dictionary += " /Length " + std::to_string(data.size()) + " >>";
      out += std::to_string(number);
      out += " 0 obj\n";
      out += dictionary;
      out += "\nstream\n";
      out += data;
      out += "\nendstream\nendobj\n";
      return at;
    };

    std::string tail;
    if (layout.xref == XrefForm::stream) {
      std::string extra = trailer_entries;
      // /Size counts the stream's own number too.
      extra.replace(extra.find(" /Size ") + 7, std::to_string(next).size(),
                    std::to_string(next + 1));
      xref_at = write_stream(rows, extra);
      tail = "startxref\n";
    } else {
      std::int64_t stream_at = -1;
      if (layout.xref == XrefForm::hybrid && !stream_rows.empty()) {
        stream_at = write_stream(stream_rows, " /Size " + std::to_string(next + 1));
        trailer_entries += " /XRefStm " + std::to_string(stream_at);
        trailer_entries.replace(trailer_entries.find(" /Size ") + 7,
                                std::to_string(next - 1).size(), std::to_string(next));
      }
      xref_at = offset_now();
      std::string table;
      if (!(newest && layout.omit_table)) {
        table = "xref\n";
        bool first_run = true;
        for (const auto& [first, count] : runs(rows)) {
          const std::uint32_t label = first_run && r == 0 && layout.table_off_by_one ? 1 : first;
          table += std::to_string(label) + " " + std::to_string(count) + "\n";
          first_run = false;
          for (const XrefRow& row : rows) {
            if (row.number >= first && row.number < first + count) {
              table += table_entry(static_cast<std::int64_t>(row.field2),
                                   static_cast<std::uint32_t>(row.field3), row.type == 1,
                                   layout.entry_bytes);
            }
          }
        }
      }
      out += table;
      tail = "trailer\n<<" + trailer_entries + " >>\nstartxref\n";
    }
    const std::int64_t written_startxref = newest && layout.startxref ? *layout.startxref : xref_at;
    const std::string ending = tail + std::to_string(written_startxref) + "\n";
    out += ending;
    if (newest && layout.duplicate_trailer) {
      out += ending;
    }
    if (!(newest && layout.omit_eof)) {
      out += "%%EOF\n";
    }
    previous_xref = xref_at;
  }
  if (length_patch) {
    const std::string total = padded_number(static_cast<std::int64_t>(out.size() - header_at), 10);
    out.replace(*length_patch, 10, total);
  }
  return out;
}

}  // namespace pdfkit::test
