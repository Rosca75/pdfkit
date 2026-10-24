// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// JsonValue is the small value tree JsonReader produces from a caller's JSON
// (options, matches, treatments, field values). Objects keep their members in
// input order, and an integer keeps its exact value beside its double, so a
// page number or an object number above 2^53 is never rounded. Asking a value
// for the wrong kind is an internal error: callers check the kind first and
// refuse the input with a message in the caller's terms.

#ifndef PDFKIT_PLATFORM_JSON_VALUE_HPP
#define PDFKIT_PLATFORM_JSON_VALUE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pdfkit::platform {

class JsonValue {
 public:
  enum class Kind : std::uint8_t { null, boolean, number, string, array, object };
  using Array = std::vector<JsonValue>;
  using Member = std::pair<std::string, JsonValue>;
  using Object = std::vector<Member>;

  JsonValue() = default;
  [[nodiscard]] static JsonValue make_boolean(bool value);
  [[nodiscard]] static JsonValue make_number(double value);
  [[nodiscard]] static JsonValue make_integer(std::int64_t value);
  [[nodiscard]] static JsonValue make_string(std::string value);
  [[nodiscard]] static JsonValue make_array(Array value);
  [[nodiscard]] static JsonValue make_object(Object value);

  [[nodiscard]] Kind kind() const noexcept { return kind_; }
  [[nodiscard]] bool as_bool() const;
  [[nodiscard]] double as_number() const;
  // The exact integer when the number was written without a fraction or an
  // exponent and fits 64 bits; no value otherwise.
  [[nodiscard]] std::optional<std::int64_t> as_int64() const;
  [[nodiscard]] const std::string& as_string() const;
  [[nodiscard]] const Array& as_array() const;
  [[nodiscard]] const Object& as_object() const;

  // The member named key of an object, or nullptr when it has none.
  [[nodiscard]] const JsonValue* find(std::string_view key) const;

  // The name of a kind, for messages ("an object", "a string").
  [[nodiscard]] static std::string_view kind_name(Kind kind) noexcept;

 private:
  void require(Kind wanted) const;

  Kind kind_ = Kind::null;
  bool boolean_ = false;
  double number_ = 0.0;
  std::optional<std::int64_t> integer_;
  std::string string_;
  Array array_;
  Object object_;
};

}  // namespace pdfkit::platform

#endif  // PDFKIT_PLATFORM_JSON_VALUE_HPP
