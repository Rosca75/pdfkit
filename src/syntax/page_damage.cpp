// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The damaged pages' JSON, written in one fixed key order by every output
// that carries it, and the merge of one stream's losses into one entry per
// class, so a form drawn twice or a lexer that met ten unknown operators in
// one stream reads as one line.

#include "syntax/page_damage.hpp"

#include <algorithm>
#include <utility>

namespace pdfkit::syntax {

PageDamage PageDamage::unchecked(std::uint32_t page, const platform::Error& error) {
  PageDamage out;
  out.page = page;
  Stream stream;
  stream.known = false;
  stream.damage = std::string(not_checked);
  stream.fault = error.failed();
  out.streams.push_back(std::move(stream));
  return out;
}

void PageDamage::add(Stream stream) {
  const auto same = std::ranges::find_if(streams, [&stream](const Stream& entry) {
    return entry.known == stream.known && entry.piece == stream.piece &&
           entry.form == stream.form && entry.form_path == stream.form_path &&
           entry.damage == stream.damage;
  });
  if (same == streams.end()) {
    streams.push_back(std::move(stream));
    return;
  }
  if (*same == stream) {
    return;
  }
  if (same->unread_bytes && stream.unread_bytes) {
    *same->unread_bytes += *stream.unread_bytes;
  } else {
    same->unread_bytes.reset();
  }
}

void PageDamage::write(platform::JsonWriter& writer, const std::vector<PageDamage>& pages) {
  writer.begin_array();
  for (const PageDamage& page : pages) {
    writer.begin_object();
    writer.key("page").unsigned_integer(page.page);
    if (page.source_page) {
      writer.key("source_page").unsigned_integer(*page.source_page);
    }
    writer.key("glyphs").unsigned_integer(page.glyphs);
    writer.key("streams").begin_array();
    for (const Stream& stream : page.streams) {
      writer.begin_object();
      writer.key("stream");
      if (stream.known) {
        writer.begin_object();
        writer.key("piece");
        if (stream.piece) {
          writer.unsigned_integer(*stream.piece);
        } else {
          writer.null();
        }
        writer.key("form");
        if (stream.form != 0) {
          writer.unsigned_integer(stream.form);
        } else {
          writer.null();
        }
        writer.key("form_path").begin_array();
        for (const std::uint32_t form : stream.form_path) {
          writer.unsigned_integer(form);
        }
        writer.end_array();
        writer.end_object();
      } else {
        writer.null();
      }
      writer.key("class").string(stream.damage);
      writer.key("fault").string(stream.fault);
      writer.key("decoded_bytes").unsigned_integer(stream.decoded_bytes);
      writer.key("unread_bytes");
      if (stream.unread_bytes) {
        writer.unsigned_integer(*stream.unread_bytes);
      } else {
        writer.null();
      }
      writer.end_object();
    }
    writer.end_array();
    if (page.text_redaction) {
      writer.key("text_redaction").boolean(*page.text_redaction);
    }
    if (page.copied_as_is) {
      writer.key("copied_as_is").boolean(*page.copied_as_is);
    }
    writer.end_object();
  }
  writer.end_array();
}

}  // namespace pdfkit::syntax
