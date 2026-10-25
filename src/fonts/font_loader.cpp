// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Every stream a font reads (program, /ToUnicode, CMap, /CIDToGIDMap) is
// decoded once per document and cached by its object number. A /ToUnicode
// written as the name Identity-H or Identity-V reads each two-byte code as its
// own UTF-16 value, as pdf.js reads it (IdentityToUnicodeMap, read). An
// embedded CMap's /UseCMap is followed eight deep at most.

#include "fonts/font_loader.hpp"

#include "fonts/cmap_parser.hpp"
#include "fonts/simple_font.hpp"
#include "fonts/type0_font.hpp"
#include "platform/error.hpp"

#include <exception>

namespace pdfkit::fonts {

namespace {

constexpr int max_use_cmap_depth = 8;

PdfFont::Kind kind_of(const syntax::Object& subtype, std::vector<std::string>& faults) {
  const std::string* name = subtype.name();
  if (name == nullptr) {
    faults.emplace_back("the font has no /Subtype; it is read as Type 1");
    return PdfFont::Kind::type1;
  }
  if (*name == "TrueType") {
    return PdfFont::Kind::true_type;
  }
  if (*name == "Type3") {
    return PdfFont::Kind::type3;
  }
  if (*name == "MMType1") {
    return PdfFont::Kind::mm_type1;
  }
  if (*name == "Type0") {
    return PdfFont::Kind::type0;
  }
  if (*name != "Type1") {
    faults.push_back("the font's /Subtype " + *name + " is unknown; it is read as Type 1");
  }
  return PdfFont::Kind::type1;
}

std::shared_ptr<Cmap> identity_to_unicode() {
  auto map = std::make_shared<Cmap>();
  map->add_codespace(0x0000, 0xFFFF, 2);
  map->add_unicode_range(0x0000, 0xFFFF, std::string(2, '\0'));
  return map;
}

}  // namespace

std::shared_ptr<PdfFont> FontLoader::load(const syntax::Object& font) {
  std::uint32_t number = 0;
  syntax::Object resolved = font;
  if (const std::optional<syntax::Object::Reference> reference = font.reference()) {
    number = reference->number;
    if (const auto found = by_object_.find(number); found != by_object_.end()) {
      return found->second;
    }
    resolved = document_->resolve(font);
  }
  std::shared_ptr<PdfFont> built;
  if (const syntax::Dictionary* dictionary = resolved.dictionary()) {
    built = build(*dictionary, number);
  } else {
    built = SimpleFont::missing(*document_, number, {"the font resource names no font dictionary"});
  }
  if (number != 0) {
    by_object_.emplace(number, built);
  }
  order_.push_back(built);
  return built;
}

std::shared_ptr<PdfFont> FontLoader::build(const syntax::Dictionary& font,
                                           std::uint32_t object_number) {
  std::vector<std::string> faults;
  try {
    const PdfFont::Kind kind = kind_of(document_->resolve_entry(font, "Subtype"), faults);
    const std::shared_ptr<const Cmap> to_unicode = [&]() -> std::shared_ptr<const Cmap> {
      const syntax::Object value = document_->resolve_entry(font, "ToUnicode");
      if (value.is_name("Identity-H") || value.is_name("Identity-V")) {
        return identity_to_unicode();
      }
      return value.stream() != nullptr ? cmap_stream(value, faults) : nullptr;
    }();
    if (kind != PdfFont::Kind::type0) {
      const syntax::Object descriptor = document_->resolve_entry(font, "FontDescriptor");
      std::shared_ptr<const FontProgram> file;
      if (const syntax::Dictionary* entries = descriptor.dictionary();
          entries != nullptr && kind != PdfFont::Kind::type3) {
        file = program(*entries, faults);
      }
      return std::make_shared<SimpleFont>(*document_, font, kind, object_number, file, to_unicode,
                                          faults);
    }
    // A composite font: the CMap, then the descendant's program and map.
    const syntax::Object encoding = document_->resolve_entry(font, "Encoding");
    std::shared_ptr<const Cmap> cmap;
    std::string predefined;
    if (const std::string* name = encoding.name()) {
      if (*name == "Identity-H" || *name == "Identity-V") {
        cmap = std::make_shared<Cmap>(Cmap::identity(*name == "Identity-V"));
      } else {
        auto map = std::make_shared<Cmap>(Type0Font::predefined_codespace(*name));
        map->set_vertical(name->ends_with("-V"));
        cmap = std::move(map);
        predefined = *name;
      }
    } else if (encoding.stream() != nullptr) {
      cmap = cmap_stream(encoding, faults);
    }
    if (!cmap) {
      faults.emplace_back("the composite font has no usable /Encoding; it is read as Identity-H");
      cmap = std::make_shared<Cmap>(Cmap::identity(false));
    }
    std::shared_ptr<const FontProgram> file;
    std::shared_ptr<const std::vector<std::uint16_t>> gid_map;
    const syntax::Object descendants = document_->resolve_entry(font, "DescendantFonts");
    syntax::Object cid_font;
    if (const syntax::Object::Array* items = descendants.array();
        items != nullptr && !items->empty()) {
      cid_font = document_->resolve(items->front());
    }
    if (const syntax::Dictionary* descendant = cid_font.dictionary()) {
      const syntax::Object descriptor = document_->resolve_entry(*descendant, "FontDescriptor");
      if (const syntax::Dictionary* entries = descriptor.dictionary()) {
        file = program(*entries, faults);
      }
      gid_map = cid_to_gid(document_->resolve_entry(*descendant, "CIDToGIDMap"), faults);
    }
    return std::make_shared<Type0Font>(*document_, font, object_number, cmap, predefined, file,
                                       gid_map, to_unicode, faults);
  } catch (const platform::Error& error) {
    if (platform::is_refusal(error.code())) {
      throw;
    }
    faults.push_back("the font could not be read: " + error.failed());
  } catch (const std::bad_alloc&) {
    throw;
  } catch (const std::exception& error) {
    faults.push_back(std::string("the font could not be read: ") + error.what());
  }
  return SimpleFont::missing(*document_, object_number, faults);
}

std::shared_ptr<const FontProgram> FontLoader::program(const syntax::Dictionary& descriptor,
                                                       std::vector<std::string>& faults) {
  constexpr std::array<std::pair<std::string_view, FontProgram::Entry>, 3> entries = {{
      {"FontFile", FontProgram::Entry::font_file},
      {"FontFile2", FontProgram::Entry::font_file2},
      {"FontFile3", FontProgram::Entry::font_file3},
  }};
  for (const auto& [key, entry] : entries) {
    const syntax::Object* raw = descriptor.find(key);
    if (raw == nullptr) {
      continue;
    }
    const syntax::Object value = document_->resolve(*raw);
    const syntax::Stream* stream = value.stream();
    if (stream == nullptr) {
      faults.push_back("the descriptor's /" + std::string(key) + " is not a stream");
      continue;
    }
    const std::uint32_t number = stream->owner().number;
    if (const auto found = programs_.find(number); number != 0 && found != programs_.end()) {
      if (const auto note = program_faults_.find(number); note != program_faults_.end()) {
        faults.push_back(note->second);
      }
      return found->second;
    }
    syntax::Filters::Result decoded = document_->decode(*stream);
    std::string fault;
    std::string subtype;
    if (const std::string* name =
            document_->resolve_entry(stream->dictionary(), "Subtype").name()) {
      subtype = *name;
    }
    if (!decoded.remaining.empty()) {
      fault = "the font program is encoded with " + decoded.remaining.front() +
              ", which is not a font filter";
    }
    std::shared_ptr<const FontProgram> read;
    if (fault.empty()) {
      FontProgram parsed =
          FontProgram::read(decoded.bytes, entry, subtype, document_->budget(), fault);
      if (parsed.kind() != PdfFont::Program::none) {
        read = std::make_shared<const FontProgram>(std::move(parsed));
      }
    }
    if (!fault.empty()) {
      faults.push_back(fault);
    }
    if (number != 0) {
      programs_.emplace(number, read);
      if (!fault.empty()) {
        program_faults_.emplace(number, fault);
      }
    }
    return read;
  }
  return nullptr;
}

std::shared_ptr<const Cmap> FontLoader::cmap_stream(const syntax::Object& value,
                                                    std::vector<std::string>& faults, int depth) {
  const syntax::Stream* stream = value.stream();
  if (stream == nullptr) {
    return nullptr;
  }
  const std::uint32_t number = stream->owner().number;
  std::shared_ptr<const Cmap> map;
  if (const auto found = cmaps_.find(number); number != 0 && found != cmaps_.end()) {
    map = found->second;
  } else {
    const syntax::Filters::Result decoded = document_->decode(*stream);
    Cmap parsed = CmapParser::parse(decoded.bytes);
    const syntax::Object use = document_->resolve_entry(stream->dictionary(), "UseCMap");
    if (use.is_name("Identity-H") || use.is_name("Identity-V")) {
      parsed.inherit(Cmap::identity(use.is_name("Identity-V")));
    } else if (use.stream() != nullptr && depth < max_use_cmap_depth) {
      if (const std::shared_ptr<const Cmap> parent = cmap_stream(use, faults, depth + 1)) {
        parsed.inherit(*parent);
      }
    }
    if (const std::optional<std::int64_t> mode =
            document_->resolve_entry(stream->dictionary(), "WMode").integer();
        mode && *mode == 1) {
      parsed.set_vertical(true);
    }
    map = std::make_shared<const Cmap>(std::move(parsed));
    if (number != 0) {
      cmaps_.emplace(number, map);
    }
  }
  if (map->malformed() > 0) {
    faults.push_back("the CMap in object " + std::to_string(number) + " has " +
                     std::to_string(map->malformed()) + " malformed entries, skipped");
  }
  return map;
}

std::shared_ptr<const std::vector<std::uint16_t>> FontLoader::cid_to_gid(
    const syntax::Object& value, std::vector<std::string>& faults) {
  const syntax::Stream* stream = value.stream();
  if (stream == nullptr) {
    if (!value.is_null() && !value.is_name("Identity")) {
      faults.emplace_back("the /CIDToGIDMap is neither Identity nor a stream; Identity is used");
    }
    return nullptr;
  }
  const std::uint32_t number = stream->owner().number;
  if (const auto found = gid_maps_.find(number); number != 0 && found != gid_maps_.end()) {
    return found->second;
  }
  const syntax::Filters::Result decoded = document_->decode(*stream);
  auto map = std::make_shared<std::vector<std::uint16_t>>();
  map->reserve(decoded.bytes.size() / 2);
  for (std::size_t i = 0; i + 1 < decoded.bytes.size(); i += 2) {
    map->push_back(static_cast<std::uint16_t>((std::to_integer<unsigned>(decoded.bytes[i]) << 8U) |
                                              std::to_integer<unsigned>(decoded.bytes[i + 1])));
  }
  std::shared_ptr<const std::vector<std::uint16_t>> shared = std::move(map);
  if (number != 0) {
    gid_maps_.emplace(number, shared);
  }
  return shared;
}

}  // namespace pdfkit::fonts
