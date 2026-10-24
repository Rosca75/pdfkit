// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// JsonWriter streams one JSON value into a string. Object members keep the
// order they are written in, strings are escaped per RFC 8259, and numbers go
// through std::to_chars (the shortest round-trip form, or a fixed number of
// decimals for rounded geometry), so identical input gives identical bytes
// whatever the locale. Misuse that would produce invalid JSON is an internal
// error rather than a malformed output.

#ifndef PDFKIT_PLATFORM_JSON_WRITER_HPP
#define PDFKIT_PLATFORM_JSON_WRITER_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::platform {

class JsonWriter {
 public:
  JsonWriter& begin_object();
  JsonWriter& end_object();
  JsonWriter& begin_array();
  JsonWriter& end_array();

  // The name of the next member of the open object.
  JsonWriter& key(std::string_view name);

  // An ill-formed UTF-8 sequence is written as U+FFFD.
  JsonWriter& string(std::string_view text);
  JsonWriter& boolean(bool value);
  JsonWriter& null();
  JsonWriter& integer(std::int64_t value);
  JsonWriter& unsigned_integer(std::uint64_t value);
  // The shortest form that reads back to the same double; -0 is written as 0.
  JsonWriter& number(double value);
  // Rounded to decimals places (0 to 17); a value that rounds to zero is
  // written without a sign.
  JsonWriter& fixed(double value, int decimals);

  // Whether one top-level value is written and every container is closed.
  [[nodiscard]] bool complete() const noexcept;
  // The text written; an internal error when the value is not complete.
  [[nodiscard]] std::string take();

 private:
  struct Frame {
    bool is_object;
    bool empty;
    bool awaiting_value;
  };

  void before_value();
  void write_string(std::string_view text);

  std::string out_;
  std::vector<Frame> stack_;
  bool root_written_ = false;
};

}  // namespace pdfkit::platform

#endif  // PDFKIT_PLATFORM_JSON_WRITER_HPP
