// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// PdfFont is what the content interpreter asks of a font, docs/DESIGN.md
// chapter 5: split a string operand into codes (one byte per code for a
// simple font, by the codespace ranges for a composite font), decode each
// code to Unicode text with the rung of the decoding ladder that answered,
// the code's advance in thousandths of text space, and the facts the glyph
// record and the placeholder need (name, kind, embedded or not, symbolic,
// bold, vertical writing, ascent and descent). A code no rung answers decodes
// to no text, is counted, and is never written as U+FFFD, a private-use
// character or "(cid:N)". A font is built by FontLoader and never throws once
// built.

#ifndef PDFKIT_FONTS_PDF_FONT_HPP
#define PDFKIT_FONTS_PDF_FONT_HPP

#include "fonts/decoding_rung.hpp"
#include "platform/byte_span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::fonts {

class PdfFont {
 public:
  // The /Subtype the font was read as; Type0 for every composite font.
  enum class Kind : std::uint8_t { type1, mm_type1, true_type, type3, type0 };

  // The program the font embeds, as its reader recognised it.
  enum class Program : std::uint8_t { none, type1, cff, true_type, open_type };

  // One code of a string operand.
  struct DecodedCode {
    std::uint32_t code = 0;
    // The bytes the code takes in the operand.
    std::size_t byte_length = 1;
    // UTF-8; empty when the rung is unreadable.
    std::string text;
    DecodingRung rung = DecodingRung::unreadable;
    // The text is a ligature's letters, so this one code drew several
    // characters.
    bool decomposed = false;
  };

  // What the ladder answers for one code.
  struct Answer {
    std::string text;
    DecodingRung rung = DecodingRung::unreadable;
    bool decomposed = false;
  };

  // Ascent and descent in thousandths of text space, and whether they are
  // the stated fallback of 0.8 and -0.2 em.
  struct VerticalMetrics {
    double ascent = 800.0;
    double descent = -200.0;
    bool fallback = true;
  };

  // The vertical-writing metrics of a code (ISO 32000-1 section 9.7.4.3,
  // read): the vertical advance w1y and the position vector (vx, vy), in
  // thousandths of text space.
  struct VerticalAdvance {
    double w1 = -1000.0;
    double vx = 500.0;
    double vy = 880.0;
  };

  PdfFont(const PdfFont&) = delete;
  PdfFont& operator=(const PdfFont&) = delete;
  PdfFont(PdfFont&&) = delete;
  PdfFont& operator=(PdfFont&&) = delete;
  virtual ~PdfFont() = default;

  // The codes of a string operand in order, each decoded; counts the
  // unreadable ones.
  [[nodiscard]] std::vector<DecodedCode> decode(platform::ByteSpan bytes);
  // The ladder's answer for one code, cached per code.
  [[nodiscard]] const Answer& answer(std::uint32_t code);
  // The advance of a code in thousandths of text space (a Type 3 width is
  // already scaled through its /FontMatrix).
  [[nodiscard]] double width(std::uint32_t code) const;
  // The vertical metrics of a code of a vertical font.
  [[nodiscard]] virtual VerticalAdvance vertical_advance(std::uint32_t code) const;

  // /BaseFont (or /Name for a Type 3 font), "unnamed" when absent.
  [[nodiscard]] const std::string& name() const noexcept { return name_; }
  [[nodiscard]] Kind kind() const noexcept { return kind_; }
  [[nodiscard]] Program program() const noexcept { return program_; }
  [[nodiscard]] bool is_embedded() const noexcept { return program_ != Program::none; }
  [[nodiscard]] bool is_symbolic() const noexcept { return symbolic_; }
  [[nodiscard]] bool is_bold() const noexcept { return bold_; }
  [[nodiscard]] bool is_vertical() const noexcept { return vertical_; }
  [[nodiscard]] const VerticalMetrics& vertical_metrics() const noexcept { return metrics_; }
  // Whether the widths are estimated: no program, no /Widths or /W, and no
  // Core 14 face (the font_not_embedded warning of docs/DESIGN.md chapter 19).
  [[nodiscard]] bool widths_estimated() const noexcept { return widths_estimated_; }
  // The font dictionary's object number, 0 for a direct dictionary.
  [[nodiscard]] std::uint32_t object_number() const noexcept { return object_number_; }
  // Codes decoded as unreadable so far.
  [[nodiscard]] std::uint64_t unreadable_count() const noexcept { return unreadable_count_; }
  // What went wrong while loading (a program that does not parse, a
  // malformed /ToUnicode), each once; the font still decodes through the
  // rungs that remain.
  [[nodiscard]] const std::vector<std::string>& faults() const noexcept { return faults_; }

  // The stable names reports use: "Type1", "MMType1", "TrueType", "Type3",
  // "Type0".
  [[nodiscard]] static std::string_view kind_name(Kind kind) noexcept;

 protected:
  PdfFont() = default;

  // The code that starts at bytes[at] and how many bytes it takes.
  [[nodiscard]] virtual std::pair<std::uint32_t, std::size_t> next_code(platform::ByteSpan bytes,
                                                                        std::size_t at) const = 0;
  // The decoding ladder for one code.
  [[nodiscard]] virtual Answer decode_code(std::uint32_t code) const = 0;
  // The advance of one code in thousandths of text space.
  [[nodiscard]] virtual double code_width(std::uint32_t code) const = 0;

  void fault(std::string message);

  std::string name_ = "unnamed";
  Kind kind_ = Kind::type1;
  Program program_ = Program::none;
  bool symbolic_ = false;
  bool bold_ = false;
  bool vertical_ = false;
  bool widths_estimated_ = false;
  std::uint32_t object_number_ = 0;
  VerticalMetrics metrics_;

 private:
  std::map<std::uint32_t, Answer> answers_;
  std::uint64_t unreadable_count_ = 0;
  std::vector<std::string> faults_;
};

}  // namespace pdfkit::fonts

#endif  // PDFKIT_FONTS_PDF_FONT_HPP
