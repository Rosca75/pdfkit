// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ContentBuilder writes a content stream from operator calls, one operator per
// line, so a test of the content lexer, and later of the interpreter, reads as
// the stream it draws. Strings are written literal with the escapes of ISO
// 32000-1 section 7.3.4.2 or hexadecimal on request. The operator set grows
// with the tests that need it.

#ifndef PDFKIT_TESTS_SUPPORT_CONTENT_BUILDER_HPP
#define PDFKIT_TESTS_SUPPORT_CONTENT_BUILDER_HPP

#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace pdfkit::test {

class ContentBuilder {
 public:
  // One element of a TJ array: a string or a number adjustment.
  using TjItem = std::variant<std::string, double>;

  ContentBuilder& begin_text();                                  // BT
  ContentBuilder& end_text();                                    // ET
  ContentBuilder& font(std::string_view name, double size);      // /name size Tf
  ContentBuilder& move(double x, double y);                      // x y Td
  ContentBuilder& show(std::string_view bytes);                  // (bytes) Tj
  ContentBuilder& show_hex(std::string_view bytes);              // <hex> Tj
  ContentBuilder& show_array(const std::vector<TjItem>& items);  // [...] TJ
  ContentBuilder& matrix(double a, double b, double c, double d, double e, double f);  // cm
  ContentBuilder& save();                                                              // q
  ContentBuilder& restore();                                                           // Q
  ContentBuilder& draw(std::string_view name);                                         // /name Do
  // BI <entries> ID <data> EI, with one space after ID.
  ContentBuilder& inline_image(std::string_view entries, std::string_view data);
  // Any text, written as given on its own line.
  ContentBuilder& raw(std::string_view text);

  [[nodiscard]] const std::string& text() const noexcept { return text_; }

  // The literal string form of bytes, "(...)" with escapes.
  [[nodiscard]] static std::string literal(std::string_view bytes);
  [[nodiscard]] static std::string number(double value);

 private:
  ContentBuilder& line(const std::string& text);

  std::string text_;
};

}  // namespace pdfkit::test

#endif  // PDFKIT_TESTS_SUPPORT_CONTENT_BUILDER_HPP
