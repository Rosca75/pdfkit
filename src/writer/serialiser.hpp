// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Serialiser writes objects in the syntax of ISO 32000-1 section 7.3 (read):
// a string in the form it was read, literal with its delimiters and
// non-printable bytes escaped or hexadecimal; a name with #xx for a delimiter,
// a number sign and every byte outside 0x21 to 0x7E (section 7.3.5); an
// integer as an integer and a real in the shortest form that reads back to the
// same value, with no exponent (section 7.3.3 has none) and a decimal point
// kept, so it reads back as a real; a dictionary in its read key order; one
// space between tokens. A reference is written under its output number, or as
// null when the object it names is not written. Numbers go through
// std::to_chars, so the bytes depend on neither locale nor platform.

#ifndef PDFKIT_WRITER_SERIALISER_HPP
#define PDFKIT_WRITER_SERIALISER_HPP

#include "syntax/object.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace pdfkit::writer {

class Serialiser {
 public:
  // The output number of a source reference; no value when the object is not
  // written.
  using Numbering = std::function<std::optional<std::uint32_t>(syntax::Object::Reference)>;

  // Appends object to out. A stream is never written here (its data belongs
  // to the file writer): passing one is an internal error.
  static void write(std::string& out, const syntax::Object& object, const Numbering& numbering);

  // Appends the text of a number.
  static void write_number(std::string& out, const syntax::Object::Number& number);
  // Appends a name, "/" included.
  static void write_name(std::string& out, std::string_view name);
  // Appends a string in its read form.
  static void write_string(std::string& out, const syntax::Object::String& string);
};

}  // namespace pdfkit::writer

#endif  // PDFKIT_WRITER_SERIALISER_HPP
