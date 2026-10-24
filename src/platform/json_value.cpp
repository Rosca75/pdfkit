// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The factories and checked accessors of platform::JsonValue.

#include "platform/json_value.hpp"

#include "platform/error.hpp"

namespace pdfkit::platform {

JsonValue JsonValue::make_boolean(bool value) {
  JsonValue result;
  result.kind_ = Kind::boolean;
  result.boolean_ = value;
  return result;
}

JsonValue JsonValue::make_number(double value) {
  JsonValue result;
  result.kind_ = Kind::number;
  result.number_ = value;
  return result;
}

JsonValue JsonValue::make_integer(std::int64_t value) {
  JsonValue result;
  result.kind_ = Kind::number;
  result.number_ = static_cast<double>(value);
  result.integer_ = value;
  return result;
}

JsonValue JsonValue::make_string(std::string value) {
  JsonValue result;
  result.kind_ = Kind::string;
  result.string_ = std::move(value);
  return result;
}

JsonValue JsonValue::make_array(Array value) {
  JsonValue result;
  result.kind_ = Kind::array;
  result.array_ = std::move(value);
  return result;
}

JsonValue JsonValue::make_object(Object value) {
  JsonValue result;
  result.kind_ = Kind::object;
  result.object_ = std::move(value);
  return result;
}

std::string_view JsonValue::kind_name(Kind kind) noexcept {
  switch (kind) {
    case Kind::null:
      return "null";
    case Kind::boolean:
      return "a boolean";
    case Kind::number:
      return "a number";
    case Kind::string:
      return "a string";
    case Kind::array:
      return "an array";
    case Kind::object:
      return "an object";
  }
  return "null";
}

void JsonValue::require(Kind wanted) const {
  if (kind_ != wanted) {
    throw Error(ErrorCode::internal,
                "a JSON value that is " + std::string(kind_name(kind_)) + " was read as " +
                    std::string(kind_name(wanted)),
                "the kind of a JSON value checked before it is read",
                "this is a defect in pdfkit; report it with the JSON that caused it");
  }
}

bool JsonValue::as_bool() const {
  require(Kind::boolean);
  return boolean_;
}

double JsonValue::as_number() const {
  require(Kind::number);
  return number_;
}

std::optional<std::int64_t> JsonValue::as_int64() const {
  require(Kind::number);
  return integer_;
}

const std::string& JsonValue::as_string() const {
  require(Kind::string);
  return string_;
}

const JsonValue::Array& JsonValue::as_array() const {
  require(Kind::array);
  return array_;
}

const JsonValue::Object& JsonValue::as_object() const {
  require(Kind::object);
  return object_;
}

const JsonValue* JsonValue::find(std::string_view key) const {
  require(Kind::object);
  for (const Member& member : object_) {
    if (member.first == key) {
      return &member.second;
    }
  }
  return nullptr;
}

}  // namespace pdfkit::platform
