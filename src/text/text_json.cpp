// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The JSON of a page. A box is [left, bottom, right, top]; an empty box and
// the unbounded clip are null. A glyph names its font by object number and
// name and its stream walk by index, form object and form path.

#include "text/text_json.hpp"

#include "fonts/decoding_rung.hpp"
#include "text/content_serialiser.hpp"

#include <cmath>

namespace pdfkit::text {

namespace {

using platform::JsonWriter;

constexpr std::array<fonts::DecodingRung, 5> rungs{
    fonts::DecodingRung::to_unicode, fonts::DecodingRung::glyph_name,
    fonts::DecodingRung::font_program, fonts::DecodingRung::actual_text,
    fonts::DecodingRung::unreadable};

// A coordinate rounded to 0.01 pt.
void coordinate(JsonWriter& writer, double value) {
  writer.number(std::round(value * 100.0) / 100.0);
}

void box(JsonWriter& writer, const Rect& rect) {
  if (rect.is_empty() || rect.is_infinite()) {
    writer.null();
    return;
  }
  writer.begin_array();
  coordinate(writer, rect.left);
  coordinate(writer, rect.bottom);
  coordinate(writer, rect.right);
  coordinate(writer, rect.top);
  writer.end_array();
}

void point(JsonWriter& writer, const Matrix::Point& value) {
  writer.begin_array();
  coordinate(writer, value.x);
  coordinate(writer, value.y);
  writer.end_array();
}

void matrix(JsonWriter& writer, const Matrix& value) {
  writer.begin_array();
  for (const double item : value.values()) {
    writer.number(std::round(item * 1e6) / 1e6);
  }
  writer.end_array();
}

void range(JsonWriter& writer, std::size_t first, std::size_t last) {
  writer.begin_array();
  writer.unsigned_integer(first);
  writer.unsigned_integer(last);
  writer.end_array();
}

void colour(JsonWriter& writer, const Colour& value) {
  writer.begin_object();
  writer.key("space").string(value.space);
  writer.key("family").string(value.family);
  writer.key("components").begin_array();
  for (const double component : value.components) {
    writer.number(std::round(component * 1e6) / 1e6);
  }
  writer.end_array();
  writer.end_object();
}

void blocks(JsonWriter& writer, const std::vector<TextLayout::Block>& list) {
  writer.begin_array();
  for (const TextLayout::Block& block : list) {
    writer.begin_object();
    writer.key("box");
    box(writer, block.box);
    writer.key("glyphs");
    range(writer, block.first, block.last);
    writer.key("lines").begin_array();
    for (const TextLayout::Line& line : block.lines) {
      writer.begin_object();
      writer.key("box");
      box(writer, line.box);
      writer.key("glyphs");
      range(writer, line.first, line.last);
      writer.key("direction").string(line.direction);
      writer.key("angle").number(line.angle);
      writer.key("words").begin_array();
      for (const TextLayout::Word& word : line.words) {
        writer.begin_object();
        writer.key("text").string(word.text);
        writer.key("box");
        box(writer, word.box);
        writer.key("glyphs");
        range(writer, word.first, word.last);
        writer.key("hyphenated").boolean(word.hyphenated);
        writer.key("hidden").begin_array();
        for (const HiddenClass value : hidden_classes) {
          if ((word.hidden & hidden_bit(value)) != 0) {
            writer.string(hidden_class_name(value));
          }
        }
        writer.end_array();
        writer.end_object();
      }
      writer.end_array();
      writer.end_object();
    }
    writer.end_array();
    writer.end_object();
  }
  writer.end_array();
}

void glyph(JsonWriter& writer, const GlyphRecord& record, const PageContent& content) {
  writer.begin_object();
  writer.key("draw").unsigned_integer(record.draw);
  writer.key("stream").begin_object();
  writer.key("walk").unsigned_integer(record.stream);
  const ContentStream* walk =
      record.stream < content.streams.size() ? &content.streams[record.stream] : nullptr;
  writer.key("form");
  if (walk == nullptr || walk->form == 0) {
    writer.null();
  } else {
    writer.unsigned_integer(walk->form);
  }
  writer.key("form_path").begin_array();
  if (walk != nullptr) {
    for (const std::uint32_t form : walk->form_path) {
      writer.unsigned_integer(form);
    }
  }
  writer.end_array();
  writer.end_object();
  writer.key("operator").unsigned_integer(record.operation);
  writer.key("operand").unsigned_integer(record.operand);
  writer.key("bytes");
  range(writer, record.code_start, record.code_end);
  writer.key("hex").boolean(record.hex_string);
  writer.key("font").begin_object();
  writer.key("object").unsigned_integer(record.font_object);
  writer.key("name").string(record.font != nullptr ? record.font->name() : std::string());
  writer.end_object();
  writer.key("code").unsigned_integer(record.code);
  writer.key("code_length").unsigned_integer(record.code_length);
  writer.key("text").string(record.text);
  writer.key("rung").string(fonts::decoding_rung_name(record.rung));
  writer.key("decomposed").boolean(record.decomposed);
  writer.key("width").number(std::round(record.width * 1000.0) / 1000.0);
  writer.key("adjustment").number(record.adjustment);
  writer.key("advance").number(std::round(record.advance * 1e6) / 1e6);
  writer.key("vertical").boolean(record.vertical);
  writer.key("text_matrix");
  matrix(writer, record.text_matrix);
  writer.key("ctm");
  matrix(writer, record.ctm);
  writer.key("char_spacing").number(record.char_spacing);
  writer.key("word_spacing").number(record.word_spacing);
  writer.key("horizontal_scaling").number(record.horizontal_scaling);
  writer.key("leading").number(record.leading);
  writer.key("rise").number(record.rise);
  writer.key("font_size").number(record.font_size);
  writer.key("render_mode").integer(record.render_mode);
  writer.key("origin");
  point(writer, record.origin);
  writer.key("advance_end");
  point(writer, record.advance_end);
  writer.key("box");
  box(writer, record.box);
  writer.key("size");
  coordinate(writer, record.size);
  writer.key("angle");
  coordinate(writer, record.angle);
  writer.key("fill");
  colour(writer, record.fill);
  writer.key("marked_content");
  if (record.tag.empty() && record.mcid < 0) {
    writer.null();
  } else {
    writer.begin_object();
    writer.key("tag").string(record.tag);
    writer.key("mcid");
    if (record.mcid < 0) {
      writer.null();
    } else {
      writer.integer(record.mcid);
    }
    writer.end_object();
  }
  writer.key("actual_text");
  if (!record.actual_text) {
    writer.null();
  } else {
    writer.begin_object();
    writer.key("span").unsigned_integer(*record.actual_text);
    writer.key("covered").boolean(record.actual_text_covered);
    writer.end_object();
  }
  writer.key("optional_content").begin_object();
  writer.key("groups").begin_array();
  for (const std::uint32_t group : record.optional_content) {
    writer.unsigned_integer(group);
  }
  writer.end_array();
  writer.key("off").boolean(record.optional_content_off);
  writer.end_object();
  writer.key("clip");
  box(writer, record.clip);
  writer.key("hidden").begin_array();
  for (const HiddenClass value : hidden_classes) {
    if (record.has(value)) {
      writer.string(hidden_class_name(value));
    }
  }
  writer.end_array();
  writer.key("duplicate_of");
  if (record.duplicate_of) {
    writer.unsigned_integer(*record.duplicate_of);
  } else {
    writer.null();
  }
  writer.key("duplicates").begin_array();
  for (const std::size_t other : record.duplicates) {
    writer.unsigned_integer(other);
  }
  writer.end_array();
  writer.end_object();
}

void optional_string(JsonWriter& writer, std::string_view name,
                     const std::optional<std::string>& value) {
  writer.key(name);
  if (value) {
    writer.string(*value);
  } else {
    writer.null();
  }
}

}  // namespace

TextJson::Reserialisation TextJson::reserialise(const PageText& page) {
  Reserialisation out;
  const PageContent& content = page.content;
  for (std::size_t i = 0; i < content.streams.size(); ++i) {
    const ContentStream& stream = content.streams[i];
    ++out.streams;
    if (stream.damaged) {
      ++out.damaged;
    }
    const ContentSerialiser::Result written = ContentSerialiser::write(stream, i, content.glyphs);
    const bool same =
        stream.bytes != nullptr && written.mismatches.empty() && written.bytes == *stream.bytes;
    if (same) {
      ++out.identical;
      continue;
    }
    if (!stream.damaged) {
      ++out.failed;
    }
    std::string what = "walk " + std::to_string(i);
    if (written.mismatches.empty()) {
      what += " writes other bytes";
    } else {
      what += ": " + written.mismatches.front();
    }
    out.mismatches.push_back(std::move(what));
  }
  return out;
}

void TextJson::write_page(JsonWriter& writer, const PageText& page, const Options& options) {
  writer.begin_object();
  writer.key("page").unsigned_integer(page.page);
  writer.key("text").string(page.text());
  writer.key("structure_order").boolean(page.structure_order);
  if (options.blocks) {
    writer.key("blocks");
    blocks(writer, page.blocks);
    writer.key("hidden_blocks");
    blocks(writer, page.hidden_blocks);
  }
  writer.key("unreadable").begin_object();
  writer.key("count").unsigned_integer(page.unreadable_count());
  writer.key("fonts").begin_array();
  for (const auto& [name, count] : page.unreadable) {
    writer.begin_object();
    writer.key("font").string(name);
    writer.key("count").unsigned_integer(count);
    writer.end_object();
  }
  writer.end_array();
  writer.end_object();
  writer.key("hidden").begin_object();
  writer.key("count").unsigned_integer(page.hidden);
  writer.key("classes").begin_object();
  for (std::size_t c = 0; c < hidden_classes.size(); ++c) {
    writer.key(hidden_class_name(hidden_classes[c])).unsigned_integer(page.hidden_counts[c]);
  }
  writer.end_object();
  writer.end_object();
  writer.key("rungs").begin_object();
  for (std::size_t r = 0; r < rungs.size(); ++r) {
    writer.key(fonts::decoding_rung_name(rungs[r])).unsigned_integer(page.rungs[r]);
  }
  writer.end_object();
  writer.key("warnings").begin_array();
  for (const PageText::Warning& warning : page.warnings) {
    writer.begin_object();
    writer.key("code").string(warning.code);
    writer.key("message").string(warning.message);
    writer.key("page").unsigned_integer(page.page);
    writer.end_object();
  }
  writer.end_array();
  if (options.reserialisation) {
    const Reserialisation check = reserialise(page);
    writer.key("reserialisation").begin_object();
    writer.key("streams").unsigned_integer(check.streams);
    writer.key("identical").unsigned_integer(check.identical);
    writer.key("damaged").unsigned_integer(check.damaged);
    writer.key("failed").unsigned_integer(check.failed);
    writer.key("mismatches").begin_array();
    for (const std::string& mismatch : check.mismatches) {
      writer.string(mismatch);
    }
    writer.end_array();
    writer.end_object();
  }
  if (options.glyphs) {
    writer.key("spans").begin_array();
    for (const MarkedSpan& span : page.content.spans) {
      writer.begin_object();
      writer.key("tag").string(span.tag);
      optional_string(writer, "actual_text", span.actual_text);
      optional_string(writer, "alt", span.alt);
      optional_string(writer, "expansion", span.expansion);
      writer.key("walk").unsigned_integer(span.stream);
      writer.key("operator").unsigned_integer(span.operation);
      writer.end_object();
    }
    writer.end_array();
    writer.key("glyphs").begin_array();
    for (const std::size_t draw : page.order) {
      glyph(writer, page.content.glyphs[draw], page.content);
    }
    writer.end_array();
  }
  writer.end_object();
}

}  // namespace pdfkit::text
