// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Object is the PDF object model of ISO 32000-1 section 7.3 (read): a small
// closed set of null, boolean, number, string, name, array, dictionary, stream
// and reference. A number keeps whether it was written as an integer, because
// object numbers, generations, /W widths and counts are integers and a real
// where an integer is expected is a parse fault; a string keeps whether it was
// read literal or hexadecimal, so the writer re-emits the form it read. The
// model holds the file's vocabulary and no semantics. Arrays, dictionaries and
// streams are shared and immutable once built, so an Object copies in constant
// time and a resolved object can be handed out freely.

#ifndef PDFKIT_SYNTAX_OBJECT_HPP
#define PDFKIT_SYNTAX_OBJECT_HPP

#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace pdfkit::syntax {

class Dictionary;
class Stream;

class Object {
 public:
  enum class Kind : std::uint8_t {
    null,
    boolean,
    number,
    string,
    name,
    array,
    dictionary,
    stream,
    reference,
  };

  // An indirect reference "n g R".
  struct Reference {
    std::uint32_t number = 0;
    std::uint32_t generation = 0;
    friend auto operator<=>(const Reference&, const Reference&) = default;
  };

  // integer holds the exact value when is_integer; value holds the number
  // either way.
  struct Number {
    double value = 0.0;
    std::int64_t integer = 0;
    bool is_integer = false;
  };

  // The decoded bytes of a string and whether it was written <hex>.
  struct String {
    std::string bytes;
    bool hex = false;
  };

  using Array = std::vector<Object>;

  // The null object.
  Object() = default;

  [[nodiscard]] static Object make_boolean(bool value);
  [[nodiscard]] static Object make_integer(std::int64_t value);
  [[nodiscard]] static Object make_real(double value);
  [[nodiscard]] static Object make_string(std::string bytes, bool hex = false);
  [[nodiscard]] static Object make_name(std::string name);
  [[nodiscard]] static Object make_array(Array items);
  [[nodiscard]] static Object make_dictionary(Dictionary dictionary);
  [[nodiscard]] static Object make_stream(Stream stream);
  [[nodiscard]] static Object make_reference(Reference reference);

  [[nodiscard]] Kind kind() const noexcept;
  [[nodiscard]] bool is_null() const noexcept { return kind() == Kind::null; }

  // Each accessor answers nothing (no value or nullptr) for another kind.
  [[nodiscard]] std::optional<bool> boolean() const noexcept;
  [[nodiscard]] const Number* number_value() const noexcept;
  // The value of an integer; a real answers nothing, as a count must be written
  // as an integer.
  [[nodiscard]] std::optional<std::int64_t> integer() const noexcept;
  // The value of any number.
  [[nodiscard]] std::optional<double> number() const noexcept;
  [[nodiscard]] const String* string() const noexcept;
  [[nodiscard]] const std::string* name() const noexcept;
  [[nodiscard]] bool is_name(std::string_view name) const noexcept;
  [[nodiscard]] const Array* array() const noexcept;
  // The dictionary of a dictionary object; a stream answers nullptr.
  [[nodiscard]] const Dictionary* dictionary() const noexcept;
  // The dictionary of a dictionary object or of a stream.
  [[nodiscard]] const Dictionary* dictionary_or_stream_dictionary() const noexcept;
  [[nodiscard]] const Stream* stream() const noexcept;
  [[nodiscard]] std::optional<Reference> reference() const noexcept;

  // The name of a kind for messages ("a dictionary").
  [[nodiscard]] static std::string_view kind_name(Kind kind) noexcept;

  // Deep equality: equal kinds and equal values; two streams are equal when
  // their dictionaries, owners and raw byte ranges are.
  friend bool operator==(const Object& a, const Object& b);

 private:
  struct NameValue {
    std::string text;
  };

  using Value =
      std::variant<std::monostate, bool, Number, String, NameValue, std::shared_ptr<const Array>,
                   std::shared_ptr<const Dictionary>, std::shared_ptr<const Stream>, Reference>;

  explicit Object(Value value);

  Value value_;
};

}  // namespace pdfkit::syntax

#endif  // PDFKIT_SYNTAX_OBJECT_HPP
