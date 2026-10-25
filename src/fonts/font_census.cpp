// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The walk keeps only what the census needs: the current font, saved and
// restored by q and Q, and the set of forms already walked. A font resource is
// looked up without resolving its entry first, so the loader caches it by
// object number.

#include "fonts/font_census.hpp"

#include "fonts/font_loader.hpp"
#include "fonts/pdf_font.hpp"
#include "fonts/simple_font.hpp"
#include "fonts/type0_font.hpp"
#include "syntax/content_lexer.hpp"

#include <map>
#include <memory>
#include <set>

namespace pdfkit::fonts {

namespace {

// Forms nest at most as deep as the interpreter allows (docs/DESIGN.md
// section 6.1).
constexpr int max_form_depth = 32;

class Walk {
 public:
  explicit Walk(syntax::Document& document) : document_(document), loader_(document) {}

  FontCensus::Report run() {
    for (const syntax::Page& page : document_.pages()) {
      text_.clear();
      const std::vector<std::vector<std::byte>> pieces =
          document_.content_pieces(page.contents, page.number);
      walk(pieces, page.resources, "page " + std::to_string(page.number), nullptr, 0);
      report_.pages.push_back(std::move(text_));
    }
    for (const std::shared_ptr<PdfFont>& font : loader_.fonts()) {
      FontCensus::Font& entry = entry_for(*font);
      entry.faults = font->faults();
    }
    report_.fonts = std::move(fonts_);
    return std::move(report_);
  }

 private:
  FontCensus::Font& entry_for(const PdfFont& font) {
    const auto found = index_.find(&font);
    if (found != index_.end()) {
      return fonts_[found->second];
    }
    FontCensus::Font entry;
    entry.object_number = font.object_number();
    entry.name = font.name();
    entry.kind = std::string(PdfFont::kind_name(font.kind()));
    entry.program = std::string(PdfFont::program_name(font.program()));
    if (const auto* composite = dynamic_cast<const Type0Font*>(&font)) {
      entry.encoding = composite->predefined().empty() ? "cmap" : composite->predefined();
      entry.to_unicode = composite->to_unicode() != nullptr;
    } else if (const auto* simple = dynamic_cast<const SimpleFont*>(&font)) {
      entry.to_unicode = simple->to_unicode() != nullptr;
    }
    index_.emplace(&font, fonts_.size());
    fonts_.push_back(std::move(entry));
    return fonts_.back();
  }

  // The raw entry of name in the resource category of resources (a
  // reference stays a reference), or null.
  syntax::Object entry(const syntax::Object& resources, std::string_view category,
                       std::string_view name) {
    const syntax::Object dictionary = document_.resolve(resources);
    const syntax::Dictionary* entries = dictionary.dictionary();
    if (entries == nullptr) {
      return {};
    }
    const syntax::Object group = document_.resolve_entry(*entries, category);
    const syntax::Dictionary* items = group.dictionary();
    if (items == nullptr) {
      return {};
    }
    const syntax::Object* value = items->find(name);
    return value != nullptr ? *value : syntax::Object();
  }

  void show(PdfFont* font, const syntax::Object& operand) {
    const syntax::Object::String* string = operand.string();
    if (string == nullptr || font == nullptr) {
      return;
    }
    const std::vector<PdfFont::DecodedCode> codes = font->decode(platform::as_bytes(string->bytes));
    FontCensus::Font& stats = entry_for(*font);
    for (const PdfFont::DecodedCode& code : codes) {
      const auto rung = static_cast<std::size_t>(code.rung);
      ++stats.codes;
      ++stats.rungs[rung];
      ++report_.codes;
      ++report_.rungs[rung];
      if (code.rung == DecodingRung::unreadable) {
        ++stats.unreadable;
        ++report_.unreadable;
      }
      text_ += code.text;
    }
  }

  // A form without /Resources draws with the resources of what draws it, as
  // syntax::Resources::for_form reads it.
  void walk(const std::vector<std::vector<std::byte>>& pieces,
            const syntax::Object& resource_dictionary, const std::string& what, PdfFont* font,
            int depth) {
    syntax::ContentLexer lexer(pieces, document_.limits(), what);
    std::vector<PdfFont*> saved;
    while (std::optional<syntax::ContentLexer::Operation> operation = lexer.next()) {
      const std::string& op = operation->op;
      const std::vector<syntax::Object>& operands = operation->operands;
      if (op == "Tf" && !operands.empty()) {
        const std::string* name = operands.front().name();
        const syntax::Object value =
            name != nullptr ? entry(resource_dictionary, "Font", *name) : syntax::Object();
        if (value.is_null()) {
          ++report_.missing_fonts;
          font = nullptr;
        } else {
          font = loader_.load(value).get();
        }
      } else if (op == "gs" && !operands.empty()) {
        // An ExtGState's /Font entry, [font size], sets the font as Tf does
        // (ISO 32000-1 section 8.4.5, table 58, read).
        const std::string* name = operands.front().name();
        const syntax::Object state = document_.resolve(
            name != nullptr ? entry(resource_dictionary, "ExtGState", *name) : syntax::Object());
        if (const syntax::Dictionary* entries = state.dictionary()) {
          const syntax::Object pair = document_.resolve_entry(*entries, "Font");
          if (const syntax::Object::Array* items = pair.array();
              items != nullptr && !items->empty()) {
            font = loader_.load(items->front()).get();
          }
        }
      } else if ((op == "Tj" || op == "'" || op == "\"") && !operands.empty()) {
        show(font, operands.back());
      } else if (op == "TJ" && !operands.empty()) {
        if (const syntax::Object::Array* items = operands.front().array()) {
          for (const syntax::Object& item : *items) {
            show(font, item);
          }
        }
      } else if (op == "q") {
        saved.push_back(font);
      } else if (op == "Q") {
        if (!saved.empty()) {
          font = saved.back();
          saved.pop_back();
        }
      } else if (op == "Do" && !operands.empty() && depth < max_form_depth) {
        const std::string* name = operands.front().name();
        if (name == nullptr) {
          continue;
        }
        const syntax::Object raw = entry(resource_dictionary, "XObject", *name);
        const std::optional<syntax::Object::Reference> reference = raw.reference();
        const syntax::Object value = document_.resolve(raw);
        const syntax::Stream* stream = value.stream();
        if (stream == nullptr ||
            !document_.resolve_entry(stream->dictionary(), "Subtype").is_name("Form")) {
          continue;
        }
        const std::uint32_t number = reference ? reference->number : stream->owner().number;
        if (!forms_.insert(number).second) {
          continue;
        }
        syntax::Object inner_dictionary =
            document_.resolve_entry(stream->dictionary(), "Resources");
        if (inner_dictionary.dictionary() == nullptr) {
          inner_dictionary = resource_dictionary;
        }
        const std::vector<std::vector<std::byte>> form_pieces{document_.decode(*stream).bytes};
        walk(form_pieces, inner_dictionary, "form " + std::to_string(number), font, depth + 1);
      }
    }
  }

  syntax::Document& document_;
  FontLoader loader_;
  FontCensus::Report report_;
  std::vector<FontCensus::Font> fonts_;
  std::map<const PdfFont*, std::size_t> index_;
  std::set<std::uint32_t> forms_;
  std::string text_;
};

}  // namespace

FontCensus::Report FontCensus::run(syntax::Document& document) {
  return Walk(document).run();
}

}  // namespace pdfkit::fonts
