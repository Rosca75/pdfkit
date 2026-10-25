// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The constructors, accessors and deep equality of the object model, and the
// dictionary and stream members that need the whole model to be complete.

#include "syntax/object.hpp"

#include "syntax/dictionary.hpp"
#include "syntax/stream.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace pdfkit::syntax {

Object::Object(Value value) : value_(std::move(value)) {}

Object Object::make_boolean(bool value) {
  return Object(Value(value));
}

Object Object::make_integer(std::int64_t value) {
  return Object(Value(Number{static_cast<double>(value), value, true}));
}

Object Object::make_real(double value) {
  return Object(Value(Number{value, 0, false}));
}

Object Object::make_string(std::string bytes, bool hex) {
  return Object(Value(String{std::move(bytes), hex}));
}

Object Object::make_name(std::string name) {
  return Object(Value(NameValue{std::move(name)}));
}

Object Object::make_array(Array items) {
  return Object(Value(std::make_shared<const Array>(std::move(items))));
}

Object Object::make_dictionary(Dictionary dictionary) {
  return Object(Value(std::make_shared<const Dictionary>(std::move(dictionary))));
}

Object Object::make_stream(Stream stream) {
  return Object(Value(std::make_shared<const Stream>(std::move(stream))));
}

Object Object::make_reference(Reference reference) {
  return Object(Value(reference));
}

Object::Kind Object::kind() const noexcept {
  // The variant's alternatives are listed in the order of Kind.
  return static_cast<Kind>(value_.index());
}

std::optional<bool> Object::boolean() const noexcept {
  if (const bool* value = std::get_if<bool>(&value_)) {
    return *value;
  }
  return std::nullopt;
}

const Object::Number* Object::number_value() const noexcept {
  return std::get_if<Number>(&value_);
}

std::optional<std::int64_t> Object::integer() const noexcept {
  if (const Number* value = number_value(); value != nullptr && value->is_integer) {
    return value->integer;
  }
  return std::nullopt;
}

std::optional<double> Object::number() const noexcept {
  if (const Number* value = number_value()) {
    return value->value;
  }
  return std::nullopt;
}

const Object::String* Object::string() const noexcept {
  return std::get_if<String>(&value_);
}

const std::string* Object::name() const noexcept {
  if (const NameValue* value = std::get_if<NameValue>(&value_)) {
    return &value->text;
  }
  return nullptr;
}

bool Object::is_name(std::string_view name) const noexcept {
  const std::string* text = this->name();
  return text != nullptr && *text == name;
}

const Object::Array* Object::array() const noexcept {
  if (const auto* value = std::get_if<std::shared_ptr<const Array>>(&value_)) {
    return value->get();
  }
  return nullptr;
}

const Dictionary* Object::dictionary() const noexcept {
  if (const auto* value = std::get_if<std::shared_ptr<const Dictionary>>(&value_)) {
    return value->get();
  }
  return nullptr;
}

const Dictionary* Object::dictionary_or_stream_dictionary() const noexcept {
  if (const Stream* value = stream()) {
    return &value->dictionary();
  }
  return dictionary();
}

const Stream* Object::stream() const noexcept {
  if (const auto* value = std::get_if<std::shared_ptr<const Stream>>(&value_)) {
    return value->get();
  }
  return nullptr;
}

std::optional<Object::Reference> Object::reference() const noexcept {
  if (const Reference* value = std::get_if<Reference>(&value_)) {
    return *value;
  }
  return std::nullopt;
}

std::string_view Object::kind_name(Kind kind) noexcept {
  switch (kind) {
    case Kind::null:
      return "null";
    case Kind::boolean:
      return "a boolean";
    case Kind::number:
      return "a number";
    case Kind::string:
      return "a string";
    case Kind::name:
      return "a name";
    case Kind::array:
      return "an array";
    case Kind::dictionary:
      return "a dictionary";
    case Kind::stream:
      return "a stream";
    case Kind::reference:
      return "a reference";
  }
  return "an unknown kind";
}

bool operator==(const Object& a, const Object& b) {
  if (a.kind() != b.kind()) {
    return false;
  }
  switch (a.kind()) {
    case Object::Kind::null:
      return true;
    case Object::Kind::boolean:
      return a.boolean() == b.boolean();
    case Object::Kind::number: {
      const Object::Number& x = *a.number_value();
      const Object::Number& y = *b.number_value();
      return x.is_integer == y.is_integer &&
             (x.is_integer ? x.integer == y.integer : x.value == y.value);
    }
    case Object::Kind::string:
      return a.string()->bytes == b.string()->bytes && a.string()->hex == b.string()->hex;
    case Object::Kind::name:
      return *a.name() == *b.name();
    case Object::Kind::array:
      return std::ranges::equal(*a.array(), *b.array());
    case Object::Kind::dictionary:
      return *a.dictionary() == *b.dictionary();
    case Object::Kind::stream:
      return *a.stream() == *b.stream();
    case Object::Kind::reference:
      return a.reference() == b.reference();
  }
  return false;
}

}  // namespace pdfkit::syntax
