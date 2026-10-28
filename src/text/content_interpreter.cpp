// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The walk of one page. Each content stream is lexed in full first, so the
// walk owns its operation list and a form drawn inside it can add walks of its
// own without moving anything the outer walk reads. Geometry follows ISO
// 32000-1 section 9.4.4 (read): a glyph's rendering matrix is
// [Tfs*Th 0 0 Tfs 0 Trise] x Tm x CTM, its box runs from its origin over its
// width and from the font's descent to its ascent, and the position moves by
// the advance after it is shown.

#include "text/content_interpreter.hpp"

#include "fonts/decoding_rung.hpp"
#include "syntax/content_lexer.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/resources.hpp"
#include "syntax/stream.hpp"
#include "text/graphics_state.hpp"
#include "text/string_operands.hpp"
#include "text/text_string.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace pdfkit::text {

namespace {

using Operation = syntax::ContentLexer::Operation;

[[nodiscard]] std::optional<double> number_at(const Operation& operation, std::size_t index) {
  if (index >= operation.operands.size()) {
    return std::nullopt;
  }
  const std::optional<double> value = operation.operands[index].number();
  if (!value || !std::isfinite(*value)) {
    return std::nullopt;
  }
  return value;
}

// The six numbers of an operation's operands, or of an array object.
[[nodiscard]] std::optional<Matrix> matrix_of(const std::vector<syntax::Object>& values) {
  if (values.size() < 6) {
    return std::nullopt;
  }
  std::array<double, 6> numbers{};
  for (std::size_t i = 0; i < 6; ++i) {
    const std::optional<double> value = values[values.size() - 6 + i].number();
    if (!value) {
      return std::nullopt;
    }
    numbers[i] = *value;
  }
  return Matrix::of(numbers);
}

[[nodiscard]] double length(const Matrix::Point& vector) noexcept {
  return std::hypot(vector.x, vector.y);
}

[[nodiscard]] double degrees(const Matrix::Point& vector) noexcept {
  if (vector.x == 0.0 && vector.y == 0.0) {
    return 0.0;
  }
  return std::atan2(vector.y, vector.x) * 180.0 / std::numbers::pi;
}

[[nodiscard]] bool is_painting(std::string_view op) noexcept {
  return op == "S" || op == "s" || op == "f" || op == "F" || op == "f*" || op == "B" ||
         op == "B*" || op == "b" || op == "b*";
}

[[nodiscard]] bool is_filling(std::string_view op) noexcept {
  return op == "f" || op == "F" || op == "f*" || op == "B" || op == "B*" || op == "b" || op == "b*";
}

[[nodiscard]] bool is_stroking(std::string_view op) noexcept {
  return op == "S" || op == "s" || op == "B" || op == "B*" || op == "b" || op == "b*";
}

// The component count of a colour-space family; 0 when it takes no plain
// components or is unknown.
[[nodiscard]] int family_count(std::string_view family) noexcept {
  if (family == "DeviceGray" || family == "CalGray" || family == "Indexed" ||
      family == "Separation") {
    return 1;
  }
  if (family == "DeviceRGB" || family == "CalRGB" || family == "Lab") {
    return 3;
  }
  if (family == "DeviceCMYK") {
    return 4;
  }
  return 0;
}

[[nodiscard]] std::string_view device_family(std::string_view name) noexcept {
  if (name == "DeviceGray" || name == "G") {
    return "DeviceGray";
  }
  if (name == "DeviceRGB" || name == "RGB") {
    return "DeviceRGB";
  }
  if (name == "DeviceCMYK" || name == "CMYK") {
    return "DeviceCMYK";
  }
  if (name == "Pattern") {
    return "Pattern";
  }
  return {};
}

}  // namespace

class ContentInterpreter::Walk {
 public:
  Walk(ContentInterpreter& owner, const syntax::Page& page)
      : owner_(owner), document_(*owner.document_), page_(page) {
    content_.page = page.number;
    content_.media_box = page.media_box;
    content_.crop_box = page.crop_box;
    content_.rotate = page.rotate;
  }

  PageContent run() {
    std::vector<std::vector<std::byte>> pieces;
    bool damaged = false;
    const syntax::Object contents = document_.resolve(page_.contents);
    const auto add_piece = [&](const syntax::Object& value) {
      const syntax::Object resolved = document_.resolve(value);
      if (const syntax::Stream* stream = resolved.stream()) {
        syntax::Filters::Result result = document_.decode(*stream, page_.number);
        damaged = damaged || result.damaged;
        pieces.push_back(std::move(result.bytes));
      }
    };
    if (const syntax::Object::Array* list = contents.array()) {
      for (const syntax::Object& item : *list) {
        add_piece(item);
      }
    } else {
      add_piece(contents);
    }
    ContentStream stream;
    stream.damaged = damaged;
    content_.streams.push_back(std::move(stream));
    const syntax::Resources resources(document_, page_.resources, page_.number);
    walk(0, pieces, resources, 0);
    return std::move(content_);
  }

 private:
  struct Marked {
    std::string tag;
    std::int64_t mcid = -1;
    std::optional<std::size_t> span;
    bool optional_content_off = false;
    std::optional<std::uint32_t> group;
  };

  // The text matrices and the text-object flag, saved across a form draw.
  struct TextPosition {
    Matrix text;
    Matrix line;
  };

  void note_damaged(std::size_t stream_index) {
    content_.streams[stream_index].damaged = true;
    content_.damaged = true;
  }

  // Lexes pieces into the walk stream_index, then interprets its operations.
  void walk(std::size_t stream_index, const std::vector<std::vector<std::byte>>& pieces,
            const syntax::Resources& resources, int depth) {
    const std::string what = content_.streams[stream_index].form == 0
                                 ? "page " + std::to_string(page_.number)
                                 : "form " + std::to_string(content_.streams[stream_index].form) +
                                       " on page " + std::to_string(page_.number);
    syntax::ContentLexer lexer(pieces, document_.limits(), what);
    std::vector<Operation> operations;
    while (std::optional<Operation> operation = lexer.next()) {
      operations.push_back(std::move(*operation));
    }
    const platform::ByteSpan joined = lexer.bytes();
    auto bytes = std::make_shared<const std::vector<std::byte>>(joined.begin(), joined.end());
    {
      ContentStream& stream = content_.streams[stream_index];
      stream.bytes = bytes;
      std::size_t start = 0;
      for (const std::vector<std::byte>& piece : pieces) {
        stream.piece_starts.push_back(start);
        start += piece.size() + 1;
      }
      if (stream.piece_starts.empty()) {
        stream.piece_starts.push_back(0);
      }
      if (stream.damaged || lexer.faults() > 0) {
        note_damaged(stream_index);
        content_.fault(what + " holds content that does not lex; it is read as far as it lexes");
      }
    }
    close_figure();
    const std::size_t marked_base = marked_.size();
    for (std::size_t index = 0; index < operations.size(); ++index) {
      operate(operations[index], index, stream_index, *bytes, resources, depth);
    }
    close_figure();
    marked_.resize(std::min(marked_.size(), marked_base));
    content_.streams[stream_index].operations = std::move(operations);
  }

  // The resource entry as written (a reference stays a reference), or null
  // with the resource_missing warning.
  syntax::Object raw_entry(const syntax::Resources& resources, std::string_view category,
                           std::string_view name) {
    if (const syntax::Dictionary* entries = resources.dictionary().dictionary()) {
      const syntax::Object group = document_.resolve_entry(*entries, category);
      if (const syntax::Dictionary* items = group.dictionary()) {
        if (const syntax::Object* value = items->find(name)) {
          if (!document_.resolve(*value).is_null()) {
            return *value;
          }
        }
      }
    }
    (void)resources.lookup(category, name);
    return {};
  }

  void operate(const Operation& operation, std::size_t index, std::size_t stream,
               const std::vector<std::byte>& bytes, const syntax::Resources& resources, int depth) {
    const std::string& op = operation.op;
    GraphicsState::State& state = state_.current();
    if (op == "q") {
      close_figure();
      if (!state_.save()) {
        content_.fault("the content nests q deeper than " +
                       std::to_string(GraphicsState::max_depth) +
                       "; the deeper states are not saved");
      }
    } else if (op == "Q") {
      close_figure();
      (void)state_.restore();
    } else if (op == "cm") {
      if (const std::optional<Matrix> matrix = matrix_of(operation.operands)) {
        state.ctm = *matrix * state.ctm;
      }
    } else if (op == "gs") {
      graphics_state_parameters(operation, index, stream, resources);
    } else if (op == "BT") {
      text_ = {};
    } else if (op == "Tc") {
      state.char_spacing = number_at(operation, 0).value_or(state.char_spacing);
    } else if (op == "Tw") {
      state.word_spacing = number_at(operation, 0).value_or(state.word_spacing);
    } else if (op == "Tz") {
      state.horizontal_scaling = number_at(operation, 0).value_or(state.horizontal_scaling);
    } else if (op == "TL") {
      state.leading = number_at(operation, 0).value_or(state.leading);
    } else if (op == "Ts") {
      state.rise = number_at(operation, 0).value_or(state.rise);
    } else if (op == "Tr") {
      if (const auto mode = number_at(operation, 0); mode && *mode >= 0 && *mode <= 7) {
        state.render_mode = static_cast<int>(*mode);
      }
    } else if (op == "Tf") {
      select_font(operation, resources);
    } else if (op == "Td") {
      move(number_at(operation, 0).value_or(0.0), number_at(operation, 1).value_or(0.0));
    } else if (op == "TD") {
      const double ty = number_at(operation, 1).value_or(0.0);
      state.leading = -ty;
      move(number_at(operation, 0).value_or(0.0), ty);
    } else if (op == "Tm") {
      if (const std::optional<Matrix> matrix = matrix_of(operation.operands)) {
        text_.text = *matrix;
        text_.line = *matrix;
      }
    } else if (op == "T*") {
      move(0.0, -state.leading);
    } else if (op == "Tj" || op == "TJ" || op == "'" || op == "\"") {
      show(operation, index, stream, bytes);
    } else if (op == "m" || op == "l") {
      path_point(operation, 0);
      path_other_ = true;
    } else if (op == "c") {
      path_point(operation, 0);
      path_point(operation, 2);
      path_point(operation, 4);
      path_other_ = true;
    } else if (op == "v" || op == "y") {
      path_point(operation, 0);
      path_point(operation, 2);
      path_other_ = true;
    } else if (op == "re") {
      path_rectangle(operation);
    } else if (op == "W" || op == "W*") {
      clip_pending_ = true;
    } else if (is_painting(op) || op == "n") {
      paint_path(op, index, stream);
    } else if (op == "sh") {
      paint_shading(index, stream);
    } else if (op == "CS" || op == "cs") {
      set_space(op == "cs" ? state.fill : state.stroke, operation, resources);
    } else if (op == "SC" || op == "SCN" || op == "sc" || op == "scn") {
      set_components(op == "sc" || op == "scn" ? state.fill : state.stroke, operation);
      if (!operation.operands.empty() && operation.operands.back().name() != nullptr) {
        note_pattern(*operation.operands.back().name(), index, stream, resources);
      }
    } else if (op == "G" || op == "g" || op == "RG" || op == "rg" || op == "K" || op == "k") {
      set_device_colour(op, operation);
    } else if (op == "BMC" || op == "BDC") {
      begin_marked(operation, index, stream, resources);
    } else if (op == "EMC") {
      if (marked_.size() > marked_floor_) {
        marked_.pop_back();
      }
    } else if (op == "Do") {
      draw_object(operation, index, stream, resources, depth);
    } else if (op == "BI") {
      inline_image(operation, index, stream, bytes, resources);
    }
    // Every other operator (ET, whose text matrices restart at BT; h, which
    // adds no point; w, J, j, M, d, ri, i, MP, DP, BX, EX, d0, d1 and unknown
    // keywords) changes nothing the records read.
  }

  void move(double tx, double ty) {
    text_.line = Matrix::translation(tx, ty) * text_.line;
    text_.text = text_.line;
  }

  // Records content the walk does not enter.
  void note_unwalked(PageContent::Unwalked::Kind kind, const syntax::Object& raw, std::size_t index,
                     std::size_t stream) {
    PageContent::Unwalked unwalked;
    unwalked.kind = kind;
    unwalked.content = document_.resolve(raw);
    unwalked.reference = raw.reference().value_or(syntax::Object::Reference{});
    if (const syntax::Dictionary* entries = unwalked.content.dictionary_or_stream_dictionary()) {
      unwalked.resources = document_.resolve_entry(*entries, "Resources");
    }
    unwalked.stream = stream;
    unwalked.operation = index;
    unwalked.form_path = content_.streams[stream].form_path;
    content_.unwalked.push_back(std::move(unwalked));
  }

  // A pattern set as a colour: a tiling pattern (a stream) holds content.
  void note_pattern(const std::string& name, std::size_t index, std::size_t stream,
                    const syntax::Resources& resources) {
    const syntax::Object raw = raw_entry(resources, "Pattern", name);
    if (document_.resolve(raw).stream() != nullptr) {
      note_unwalked(PageContent::Unwalked::Kind::pattern, raw, index, stream);
    }
  }

  void graphics_state_parameters(const Operation& operation, std::size_t index, std::size_t stream,
                                 const syntax::Resources& resources) {
    const std::string* name =
        operation.operands.empty() ? nullptr : operation.operands.front().name();
    if (name == nullptr) {
      return;
    }
    const syntax::Object parameters = document_.resolve(raw_entry(resources, "ExtGState", *name));
    const syntax::Dictionary* entries = parameters.dictionary();
    if (entries == nullptr) {
      return;
    }
    GraphicsState::State& state = state_.current();
    if (const syntax::Object* font = entries->find("Font")) {
      // [font size] (ISO 32000-1 section 8.4.5, table 58, read).
      const syntax::Object pair = document_.resolve(*font);
      if (const syntax::Object::Array* items = pair.array();
          items != nullptr && items->size() >= 2) {
        state.font = owner_.loader_->load((*items)[0]);
        state.font_size = document_.resolve((*items)[1]).number().value_or(state.font_size);
        state.font_resource = *name;
      }
    }
    if (const auto alpha = document_.resolve_entry(*entries, "ca").number()) {
      state.fill_alpha = *alpha;
    }
    if (entries->contains("SMask")) {
      const syntax::Object mask = document_.resolve_entry(*entries, "SMask");
      state.soft_mask = !mask.is_name("None");
      if (const syntax::Dictionary* mask_entries = mask.dictionary()) {
        if (const syntax::Object* group = mask_entries->find("G")) {
          note_unwalked(PageContent::Unwalked::Kind::soft_mask, *group, index, stream);
        }
      }
    }
    if (entries->contains("BM")) {
      syntax::Object mode = document_.resolve_entry(*entries, "BM");
      if (const syntax::Object::Array* modes = mode.array(); modes != nullptr && !modes->empty()) {
        mode = modes->front();
      }
      state.normal_blend = mode.is_name("Normal") || mode.is_name("Compatible");
    }
  }

  void select_font(const Operation& operation, const syntax::Resources& resources) {
    GraphicsState::State& state = state_.current();
    state.font_size = number_at(operation, 1).value_or(state.font_size);
    const std::string* name =
        operation.operands.empty() ? nullptr : operation.operands.front().name();
    if (name == nullptr) {
      return;
    }
    const syntax::Object entry = raw_entry(resources, "Font", *name);
    state.font = entry.is_null() ? nullptr : owner_.loader_->load(entry);
    state.font_resource = *name;
  }

  // The colour space a CS or cs operand names: its name, family and count.
  void set_space(Colour& colour, const Operation& operation, const syntax::Resources& resources) {
    const std::string* name =
        operation.operands.empty() ? nullptr : operation.operands.front().name();
    if (name == nullptr) {
      return;
    }
    colour.space = *name;
    colour.family = std::string(device_family(*name));
    colour.count = family_count(colour.family);
    if (colour.family.empty()) {
      resolve_space(colour, document_.resolve(raw_entry(resources, "ColorSpace", *name)));
    }
    colour.reset();
  }

  void resolve_space(Colour& colour, const syntax::Object& value) {
    if (const std::string* name = value.name()) {
      colour.family = std::string(device_family(*name));
      colour.count = family_count(colour.family);
      return;
    }
    const syntax::Object::Array* items = value.array();
    if (items == nullptr || items->empty()) {
      return;
    }
    // The resolved objects are held while a pointer into them is read.
    const syntax::Object family_value = document_.resolve(items->front());
    const std::string* family = family_value.name();
    if (family == nullptr) {
      return;
    }
    colour.family = *family;
    colour.count = family_count(*family);
    if (*family == "ICCBased" && items->size() > 1) {
      const syntax::Object profile = document_.resolve((*items)[1]);
      if (const syntax::Dictionary* entries = profile.dictionary_or_stream_dictionary()) {
        colour.count =
            static_cast<int>(document_.resolve_entry(*entries, "N").integer().value_or(0));
      }
    } else if (*family == "DeviceN" && items->size() > 1) {
      const syntax::Object names_value = document_.resolve((*items)[1]);
      if (const syntax::Object::Array* names = names_value.array()) {
        colour.count = static_cast<int>(names->size());
      }
    } else if (!device_family(*family).empty()) {
      colour.family = std::string(device_family(*family));
      colour.count = family_count(colour.family);
    }
  }

  static void set_components(Colour& colour, const Operation& operation) {
    std::vector<double> values;
    for (const syntax::Object& operand : operation.operands) {
      if (const auto value = operand.number()) {
        values.push_back(*value);
      }
    }
    colour.components = std::move(values);
  }

  void set_device_colour(const std::string& op, const Operation& operation) {
    GraphicsState::State& state = state_.current();
    const bool fill = op == "g" || op == "rg" || op == "k";
    Colour& colour = fill ? state.fill : state.stroke;
    const char kind = static_cast<char>(std::tolower(static_cast<unsigned char>(op[0])));
    colour.family = kind == 'g' ? "DeviceGray" : (kind == 'r' ? "DeviceRGB" : "DeviceCMYK");
    colour.space = colour.family;
    colour.count = family_count(colour.family);
    set_components(colour, operation);
  }

  void begin_marked(const Operation& operation, std::size_t index, std::size_t stream,
                    const syntax::Resources& resources) {
    Marked marked;
    if (!operation.operands.empty()) {
      if (const std::string* tag = operation.operands.front().name()) {
        marked.tag = *tag;
      }
    }
    if (!marked_.empty()) {
      marked.optional_content_off = marked_.back().optional_content_off;
      marked.span = marked_.back().span;
      marked.group = marked_.back().group;
    }
    if (operation.op == "BDC" && operation.operands.size() >= 2) {
      syntax::Object properties = operation.operands[1];
      if (const std::string* name = properties.name()) {
        properties = raw_entry(resources, "Properties", *name);
      }
      if (marked.tag == "OC") {
        if (owner_.optional_content_->hidden(properties)) {
          marked.optional_content_off = true;
        }
        if (const auto reference = properties.reference()) {
          marked.group = reference->number;
        }
      }
      const syntax::Object resolved = document_.resolve(properties);
      if (const syntax::Dictionary* entries = resolved.dictionary()) {
        marked.mcid = document_.resolve_entry(*entries, "MCID").integer().value_or(-1);
        span_of(marked, *entries, index, stream);
      }
    }
    marked_.push_back(std::move(marked));
  }

  void span_of(Marked& marked, const syntax::Dictionary& entries, std::size_t index,
               std::size_t stream) {
    const auto text_of = [&](std::string_view key) -> std::optional<std::string> {
      const syntax::Object value = document_.resolve_entry(entries, key);
      if (const syntax::Object::String* string = value.string()) {
        return TextString::to_utf8(string->bytes);
      }
      return std::nullopt;
    };
    MarkedSpan span;
    span.tag = marked.tag;
    span.actual_text = text_of("ActualText");
    span.alt = text_of("Alt");
    span.expansion = text_of("E");
    if (!span.actual_text && !span.alt && !span.expansion) {
      return;
    }
    span.stream = stream;
    span.operation = index;
    span.parent = marked.span;
    content_.spans.push_back(std::move(span));
    if (content_.spans.back().actual_text && !marked.span) {
      marked.span = content_.spans.size() - 1;
    }
  }

  void path_point(const Operation& operation, std::size_t at) {
    const auto x = number_at(operation, at);
    const auto y = number_at(operation, at + 1);
    if (!x || !y) {
      return;
    }
    const Matrix::Point point = state_.current().ctm.apply(*x, *y);
    path_.add(point.x, point.y);
  }

  void path_rectangle(const Operation& operation) {
    const auto x = number_at(operation, 0);
    const auto y = number_at(operation, 1);
    const auto w = number_at(operation, 2);
    const auto h = number_at(operation, 3);
    if (!x || !y || !w || !h) {
      return;
    }
    path_.add(Rect::transformed(*x, *y, *x + *w, *y + *h, state_.current().ctm));
    ++path_rectangles_;
  }

  void close_figure() { figure_open_ = false; }

  void add_paint(FigureRecord::Paint paint, std::size_t stream) {
    if (!figure_open_ || figure_stream_ != stream || figure_depth_ != state_.depth() ||
        content_.figures.empty()) {
      FigureRecord figure;
      figure.page = page_.number;
      figure.stream = stream;
      figure.first_operation = paint.operation;
      content_.figures.push_back(std::move(figure));
      figure_open_ = true;
      figure_stream_ = stream;
      figure_depth_ = state_.depth();
    }
    FigureRecord& figure = content_.figures.back();
    figure.last_operation = paint.operation;
    figure.box.add(paint.box);
    figure.paints.push_back(paint);
  }

  void paint_path(const std::string& op, std::size_t index, std::size_t stream) {
    GraphicsState::State& state = state_.current();
    if (op != "n") {
      FigureRecord::Paint paint;
      paint.operation = index;
      paint.sequence = sequence_++;
      paint.box = path_.intersection(state.clip);
      if (paint.box.is_empty()) {
        paint.box = Rect::empty();
      }
      paint.filled = is_filling(op);
      paint.stroked = is_stroking(op);
      paint.white = state.fill.is_white();
      paint.opaque = state.fill_alpha >= 1.0 && !state.soft_mask && state.normal_blend;
      paint.rectangular = path_rectangles_ == 1 && !path_other_ && state.ctm.axis_aligned();
      add_paint(paint, stream);
    }
    if (clip_pending_) {
      state.clip = state.clip.intersection(path_.is_empty() ? Rect::empty() : path_);
    }
    path_ = Rect::empty();
    path_rectangles_ = 0;
    path_other_ = false;
    clip_pending_ = false;
  }

  void paint_shading(std::size_t index, std::size_t stream) {
    const GraphicsState::State& state = state_.current();
    if (state.clip.is_infinite() || state.clip.is_empty()) {
      return;
    }
    FigureRecord::Paint paint;
    paint.operation = index;
    paint.sequence = sequence_++;
    paint.box = state.clip;
    paint.filled = true;
    paint.opaque = state.fill_alpha >= 1.0 && !state.soft_mask && state.normal_blend;
    add_paint(paint, stream);
  }

  [[nodiscard]] bool hidden_by_marked_content() const {
    return (!marked_.empty() && marked_.back().optional_content_off) || xobject_off_ > 0;
  }

  void draw_object(const Operation& operation, std::size_t index, std::size_t stream,
                   const syntax::Resources& resources, int depth) {
    const std::string* name =
        operation.operands.empty() ? nullptr : operation.operands.front().name();
    if (name == nullptr) {
      return;
    }
    const syntax::Object raw = raw_entry(resources, "XObject", *name);
    const syntax::Object value = document_.resolve(raw);
    const syntax::Stream* object = value.stream();
    if (object == nullptr) {
      return;
    }
    const syntax::Dictionary& entries = object->dictionary();
    const syntax::Object subtype = document_.resolve_entry(entries, "Subtype");
    const bool off =
        entries.contains("OC") && owner_.optional_content_->hidden(*entries.find("OC"));
    xobject_off_ += off ? 1 : 0;
    if (subtype.is_name("Image")) {
      ImageOccurrence occurrence = occurrence_at(index, stream);
      occurrence.kind = ImageOccurrence::Kind::xobject;
      occurrence.reference = raw.reference().value_or(syntax::Object::Reference{});
      occurrence.resource_name = *name;
      occurrence.image = value;
      add_image(std::move(occurrence));
    } else if (subtype.is_name("Form")) {
      draw_form(raw, *object, index, stream, resources, depth);
    }
    xobject_off_ -= off ? 1 : 0;
  }

  ImageOccurrence occurrence_at(std::size_t index, std::size_t stream) {
    const GraphicsState::State& state = state_.current();
    ImageOccurrence occurrence;
    occurrence.page = page_.number;
    occurrence.stream = stream;
    occurrence.operation = index;
    occurrence.form_path = content_.streams[stream].form_path;
    occurrence.ctm = state.ctm;
    occurrence.box = Rect::transformed(0.0, 0.0, 1.0, 1.0, state.ctm);
    occurrence.clip = state.clip;
    occurrence.optional_content_off = hidden_by_marked_content();
    return occurrence;
  }

  void add_image(ImageOccurrence occurrence) {
    occurrence.ordinal = content_.images.size();
    occurrence.sequence = sequence_++;
    content_.images.push_back(std::move(occurrence));
  }

  void inline_image(const Operation& operation, std::size_t index, std::size_t stream,
                    const std::vector<std::byte>& bytes, const syntax::Resources& resources) {
    ImageOccurrence occurrence = occurrence_at(index, stream);
    occurrence.kind = ImageOccurrence::Kind::inline_image;
    if (const syntax::Dictionary* entries = resources.dictionary().dictionary()) {
      occurrence.colour_spaces = document_.resolve_entry(*entries, "ColorSpace");
    }
    occurrence.dictionary =
        operation.operands.empty() ? syntax::Object() : operation.operands.front();
    occurrence.data_start = std::min(operation.data_start, bytes.size());
    occurrence.data_end = std::clamp(operation.data_end, occurrence.data_start, bytes.size());
    occurrence.data = std::make_shared<const std::vector<std::byte>>(
        bytes.begin() + static_cast<std::ptrdiff_t>(occurrence.data_start),
        bytes.begin() + static_cast<std::ptrdiff_t>(occurrence.data_end));
    add_image(std::move(occurrence));
  }

  void draw_form(const syntax::Object& raw, const syntax::Stream& object, std::size_t index,
                 std::size_t stream, const syntax::Resources& resources, int depth) {
    const std::uint32_t number = raw.reference() ? raw.reference()->number : object.owner().number;
    const std::vector<std::uint32_t>& path = content_.streams[stream].form_path;
    if (std::ranges::find(path, number) != path.end()) {
      content_.fault("form " + std::to_string(number) + " draws itself (operator " +
                     std::to_string(index) + "); that draw is skipped");
      return;
    }
    if (depth + 1 > max_form_depth) {
      content_.fault("forms nest deeper than " + std::to_string(max_form_depth) + " at form " +
                     std::to_string(number) + "; the deeper draws are skipped");
      return;
    }
    std::shared_ptr<const std::vector<std::byte>> bytes;
    bool damaged = false;
    if (const auto found = owner_.forms_.find(number); found != owner_.forms_.end()) {
      bytes = found->second;
      damaged = owner_.form_damaged_[number];
    } else {
      syntax::Filters::Result result = document_.decode(object, page_.number);
      bytes = std::make_shared<const std::vector<std::byte>>(std::move(result.bytes));
      damaged = result.damaged;
      owner_.forms_.emplace(number, bytes);
      owner_.form_damaged_[number] = damaged;
    }
    const syntax::Dictionary& entries = object.dictionary();
    close_figure();
    const TextPosition saved_text = text_;
    const std::size_t saved_floor = marked_floor_;
    (void)state_.save();
    GraphicsState::State& state = state_.current();
    const syntax::Object matrix_value = document_.resolve_entry(entries, "Matrix");
    if (const syntax::Object::Array* values = matrix_value.array()) {
      if (const std::optional<Matrix> matrix = matrix_of(*values)) {
        state.ctm = *matrix * state.ctm;
      }
    }
    const syntax::Object box = document_.resolve_entry(entries, "BBox");
    if (const syntax::Object::Array* values = box.array();
        values != nullptr && values->size() == 4) {
      const auto x0 = (*values)[0].number();
      const auto y0 = (*values)[1].number();
      const auto x1 = (*values)[2].number();
      const auto y1 = (*values)[3].number();
      if (x0 && y0 && x1 && y1) {
        state.clip = state.clip.intersection(Rect::transformed(*x0, *y0, *x1, *y1, state.ctm));
      }
    }
    ContentStream walk_stream;
    walk_stream.form = number;
    walk_stream.form_path = path;
    walk_stream.form_path.push_back(number);
    walk_stream.parent = stream;
    walk_stream.drawn_at = index;
    walk_stream.matrix = state.ctm;
    walk_stream.damaged = damaged;
    content_.streams.push_back(std::move(walk_stream));
    const std::size_t walk_index = content_.streams.size() - 1;
    if (damaged) {
      note_damaged(walk_index);
    }
    marked_floor_ = marked_.size();
    const std::vector<std::vector<std::byte>> pieces{*bytes};
    walk(walk_index, pieces, resources.for_form(entries), depth + 1);
    marked_floor_ = saved_floor;
    (void)state_.restore();
    text_ = saved_text;
    close_figure();
  }

  // The string operands of a text-showing operation in order (an array's
  // strings in place of the array), each with its operand and array index.
  struct Shown {
    const syntax::Object::String* string = nullptr;
    std::size_t operand = 0;
    std::size_t item = 0;
  };

  void show(const Operation& operation, std::size_t index, std::size_t stream,
            const std::vector<std::byte>& bytes) {
    GraphicsState::State& state = state_.current();
    const std::string& op = operation.op;
    if (op == "'" || op == "\"") {
      if (op == "\"") {
        state.word_spacing = number_at(operation, 0).value_or(state.word_spacing);
        state.char_spacing = number_at(operation, 1).value_or(state.char_spacing);
      }
      move(0.0, -state.leading);
    }
    // Every string among the operands, for the byte map, and the ones shown.
    std::vector<Shown> strings;
    for (std::size_t i = 0; i < operation.operands.size(); ++i) {
      const syntax::Object& operand = operation.operands[i];
      if (const syntax::Object::String* string = operand.string()) {
        strings.push_back({string, i, 0});
      } else if (const syntax::Object::Array* items = operand.array()) {
        for (std::size_t j = 0; j < items->size(); ++j) {
          if (const syntax::Object::String* item = (*items)[j].string()) {
            strings.push_back({item, i, j});
          }
        }
      }
    }
    const std::vector<StringOperands::Token> tokens =
        StringOperands::scan(bytes, operation.start, operation.operator_start);
    const bool mapped = tokens.size() == strings.size();
    if (!mapped) {
      content_.fault("the string operands of operator " + std::to_string(index) +
                     " cannot be placed on their bytes; its glyphs carry the operator's range");
    }
    if (operation.operands.empty()) {
      return;
    }
    if (op == "TJ") {
      const std::size_t last = operation.operands.size() - 1;
      const syntax::Object::Array* items = operation.operands[last].array();
      if (items == nullptr) {
        return;
      }
      double adjustment = 0.0;
      std::size_t string_index = 0;
      while (string_index < strings.size() && strings[string_index].operand != last) {
        ++string_index;
      }
      for (std::size_t j = 0; j < items->size(); ++j) {
        const syntax::Object& item = (*items)[j];
        if (const auto value = item.number(); value && std::isfinite(*value)) {
          adjust(*value);
          adjustment += *value;
        } else if (item.string() != nullptr) {
          const StringOperands::Token* token =
              mapped && string_index < tokens.size() ? &tokens[string_index] : nullptr;
          show_string(*item.string(), j, token, index, stream, operation, adjustment);
          adjustment = 0.0;
          ++string_index;
        }
      }
      return;
    }
    const std::size_t shown = op == "\"" ? 2 : operation.operands.size() - 1;
    if (shown >= operation.operands.size() || operation.operands[shown].string() == nullptr) {
      return;
    }
    const StringOperands::Token* token = nullptr;
    if (mapped) {
      for (std::size_t i = 0; i < strings.size(); ++i) {
        if (strings[i].operand == shown) {
          token = &tokens[i];
        }
      }
    }
    show_string(*operation.operands[shown].string(), shown, token, index, stream, operation, 0.0);
  }

  // A TJ number: moves the position without a glyph.
  void adjust(double value) {
    const GraphicsState::State& state = state_.current();
    const bool vertical = state.font != nullptr && state.font->is_vertical();
    if (vertical) {
      text_.text = Matrix::translation(0.0, -value / 1000.0 * state.font_size) * text_.text;
    } else {
      text_.text = Matrix::translation(
                       -value / 1000.0 * state.font_size * state.horizontal_scaling / 100.0, 0.0) *
                   text_.text;
    }
  }

  void show_string(const syntax::Object::String& string, std::size_t operand,
                   const StringOperands::Token* token, std::size_t index, std::size_t stream,
                   const Operation& operation, double adjustment) {
    GraphicsState::State& state = state_.current();
    if (state.font == nullptr) {
      return;
    }
    fonts::PdfFont& font = *state.font;
    const std::vector<fonts::PdfFont::DecodedCode> codes =
        font.decode(platform::as_bytes(string.bytes));
    const bool ranges = token != nullptr && token->ranges.size() == string.bytes.size();
    std::size_t offset = 0;
    bool first = true;
    for (const fonts::PdfFont::DecodedCode& code : codes) {
      // Built in place: a record is large and a page can hold millions.
      GlyphRecord& record = content_.glyphs.emplace_back();
      record.page = page_.number;
      record.stream = stream;
      record.operation = index;
      record.operand = operand;
      record.hex_string = token != nullptr ? token->hex : string.hex;
      if (ranges && code.byte_length > 0 && offset + code.byte_length <= token->ranges.size()) {
        record.code_start = token->ranges[offset].first;
        record.code_end = token->ranges[offset + code.byte_length - 1].second;
      } else {
        record.code_start = operation.start;
        record.code_end = operation.operator_start;
      }
      offset += code.byte_length;
      record.font = state.font;
      record.font_object = font.object_number();
      record.code = code.code;
      record.code_length = code.byte_length;
      record.text = code.text;
      record.rung = code.rung;
      record.decomposed = code.decomposed;
      record.adjustment = first ? adjustment : 0.0;
      first = false;
      place(record, font, code);
      if (code.rung == fonts::DecodingRung::unreadable) {
        ++content_.unreadable[font.name()];
      }
      if (font.widths_estimated()) {
        content_.estimated_widths.insert(font.name());
      }
      record.draw = content_.glyphs.size() - 1;
      record.sequence = sequence_++;
    }
  }

  // Fills the geometry and the drawing context of a record and advances the
  // text matrix past it.
  void place(GlyphRecord& record, const fonts::PdfFont& font,
             const fonts::PdfFont::DecodedCode& code) {
    const GraphicsState::State& state = state_.current();
    const double size = state.font_size;
    const double scale = state.horizontal_scaling / 100.0;
    const bool word = code.byte_length == 1 && code.code == 32;
    const double spacing = state.char_spacing + (word ? state.word_spacing : 0.0);
    const fonts::PdfFont::VerticalMetrics& metrics = font.vertical_metrics();
    const double ascent = std::max(metrics.ascent, metrics.descent) / 1000.0 * size;
    const double descent = std::min(metrics.ascent, metrics.descent) / 1000.0 * size;
    const Matrix matrix = text_.text * state.ctm;
    record.text_matrix = text_.text;
    record.ctm = state.ctm;
    record.char_spacing = state.char_spacing;
    record.word_spacing = state.word_spacing;
    record.horizontal_scaling = state.horizontal_scaling;
    record.leading = state.leading;
    record.rise = state.rise;
    record.font_size = size;
    record.render_mode = state.render_mode;
    record.vertical = font.is_vertical();
    if (record.vertical) {
      const fonts::PdfFont::VerticalAdvance vertical = font.vertical_advance(code.code);
      const double w0 = font.width(code.code);
      record.width = vertical.w1;
      const double ty = (vertical.w1 / 1000.0 * size) + spacing;
      record.advance = ty;
      // The glyph's cell in vertical writing: across, its width centred on
      // the position vector's vx; down the column, the vertical advance from
      // the current point (the ascent-and-descent box of horizontal writing
      // has no meaning along a column).
      const double x0 = -vertical.vx / 1000.0 * size;
      record.box = Rect::transformed(x0, state.rise + (vertical.w1 / 1000.0 * size),
                                     x0 + (w0 / 1000.0 * size), state.rise, matrix);
      record.origin = matrix.apply(0.0, state.rise);
      record.advance_end = matrix.apply(0.0, ty + state.rise);
      record.size = std::abs(size) * length(matrix.apply_vector(1.0, 0.0));
      // The direction the advance runs in: a negative size runs it backwards.
      record.angle = degrees(matrix.apply_vector(0.0, size < 0.0 ? 1.0 : -1.0));
      text_.text = Matrix::translation(0.0, ty) * text_.text;
    } else {
      const double w0 = font.width(code.code);
      record.width = w0;
      const double tx = ((w0 / 1000.0 * size) + spacing) * scale;
      record.advance = tx;
      record.box = Rect::transformed(0.0, state.rise + descent, w0 / 1000.0 * size * scale,
                                     state.rise + ascent, matrix);
      record.origin = matrix.apply(0.0, state.rise);
      record.advance_end = matrix.apply(tx, state.rise);
      record.size = std::abs(size) * length(matrix.apply_vector(0.0, 1.0));
      // The direction the advance runs in: a negative size or scaling runs
      // it backwards (the text is drawn turned half a turn).
      record.angle = degrees(matrix.apply_vector((scale < 0.0) != (size < 0.0) ? -1.0 : 1.0, 0.0));
      text_.text = Matrix::translation(tx, 0.0) * text_.text;
    }
    record.fill = state.fill;
    record.stroke_white = state.stroke.is_white();
    record.clip = state.clip;
    for (const Marked& marked : std::views::reverse(marked_)) {
      if (!marked.tag.empty() && marked.tag != "OC") {
        record.tag = marked.tag;
        record.mcid = marked.mcid;
        break;
      }
    }
    for (const Marked& marked : std::views::reverse(marked_)) {
      if (marked.mcid >= 0) {
        record.mcid = marked.mcid;
        break;
      }
    }
    if (!marked_.empty()) {
      record.actual_text = marked_.back().span;
      if (const std::optional<std::uint32_t> group = marked_.back().group) {
        record.optional_content.push_back(*group);
      }
    }
    record.optional_content_off = hidden_by_marked_content();
  }

  ContentInterpreter& owner_;
  syntax::Document& document_;
  const syntax::Page& page_;
  PageContent content_;
  GraphicsState state_;
  TextPosition text_;
  std::vector<Marked> marked_;
  std::size_t marked_floor_ = 0;
  int xobject_off_ = 0;
  std::size_t sequence_ = 0;
  Rect path_ = Rect::empty();
  int path_rectangles_ = 0;
  bool path_other_ = false;
  bool clip_pending_ = false;
  bool figure_open_ = false;
  std::size_t figure_stream_ = 0;
  std::size_t figure_depth_ = 0;
};

ContentInterpreter::ContentInterpreter(syntax::Document& document, fonts::FontLoader& loader,
                                       const OptionalContent& optional_content)
    : document_(&document), loader_(&loader), optional_content_(&optional_content) {}

PageContent ContentInterpreter::run(const syntax::Page& page) {
  return Walk(*this, page).run();
}

}  // namespace pdfkit::text
