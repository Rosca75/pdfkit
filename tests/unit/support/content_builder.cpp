// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The operator writers of ContentBuilder. Numbers are written with
// std::to_chars, so the stream is the same whatever the locale.

#include "support/content_builder.hpp"

#include <array>
#include <charconv>

namespace pdfkit::test {

std::string ContentBuilder::number(double value) {
  std::array<char, 64> buffer{};
  const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  return {buffer.data(), result.ptr};
}

std::string ContentBuilder::literal(std::string_view bytes) {
  std::string out = "(";
  for (const char value : bytes) {
    if (value == '(' || value == ')' || value == '\\') {
      out.push_back('\\');
    }
    out.push_back(value);
  }
  return out + ")";
}

ContentBuilder& ContentBuilder::line(const std::string& text) {
  text_ += text;
  text_ += '\n';
  return *this;
}

ContentBuilder& ContentBuilder::begin_text() {
  return line("BT");
}

ContentBuilder& ContentBuilder::end_text() {
  return line("ET");
}

ContentBuilder& ContentBuilder::font(std::string_view name, double size) {
  return line("/" + std::string(name) + " " + number(size) + " Tf");
}

ContentBuilder& ContentBuilder::move(double x, double y) {
  return line(number(x) + " " + number(y) + " Td");
}

ContentBuilder& ContentBuilder::show(std::string_view bytes) {
  return line(literal(bytes) + " Tj");
}

ContentBuilder& ContentBuilder::show_hex(std::string_view bytes) {
  static constexpr char digits[] = "0123456789ABCDEF";
  std::string hex = "<";
  for (const char value : bytes) {
    const auto byte = static_cast<unsigned char>(value);
    hex.push_back(digits[byte >> 4U]);
    hex.push_back(digits[byte & 0x0FU]);
  }
  return line(hex + "> Tj");
}

ContentBuilder& ContentBuilder::show_array(const std::vector<TjItem>& items) {
  std::string out = "[";
  for (const TjItem& item : items) {
    if (const auto* bytes = std::get_if<std::string>(&item)) {
      out += literal(*bytes);
    } else {
      out += " " + number(std::get<double>(item)) + " ";
    }
  }
  return line(out + "] TJ");
}

ContentBuilder& ContentBuilder::matrix(double a, double b, double c, double d, double e, double f) {
  return line(number(a) + " " + number(b) + " " + number(c) + " " + number(d) + " " + number(e) +
              " " + number(f) + " cm");
}

ContentBuilder& ContentBuilder::save() {
  return line("q");
}

ContentBuilder& ContentBuilder::restore() {
  return line("Q");
}

ContentBuilder& ContentBuilder::draw(std::string_view name) {
  return line("/" + std::string(name) + " Do");
}

ContentBuilder& ContentBuilder::inline_image(std::string_view entries, std::string_view data) {
  text_ += "BI " + std::string(entries) + " ID ";
  text_ += data;
  return line("\nEI");
}

ContentBuilder& ContentBuilder::raw(std::string_view text) {
  return line(std::string(text));
}

}  // namespace pdfkit::test
