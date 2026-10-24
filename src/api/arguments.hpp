// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The argument checks every exported function runs before it does anything:
// the output buffer, the input bytes, the required strings and the JSON
// inputs. Each failure is an invalid_argument error naming the symbol and the
// parameter, so a caller in any language sees which argument to fix.

#ifndef PDFKIT_API_ARGUMENTS_HPP
#define PDFKIT_API_ARGUMENTS_HPP

#include "pdfkit/pdfkit.h"

#include "platform/json_value.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace pdfkit::api {

// The buffer the result or the error is written into must exist.
void require_out(const pdfkit_buffer* out, std::string_view symbol, std::string_view parameter);

// bytes may be null only when size is zero.
void require_bytes(const std::uint8_t* bytes, std::size_t size, std::string_view symbol);

// A required, NUL-terminated UTF-8 string.
std::string_view require_string(const char* text, std::string_view symbol,
                                std::string_view parameter);

// A JSON object given by the caller; a null pointer is the empty object when
// the parameter is optional and an error when it is required.
platform::JsonValue parse_json_object(const char* json, std::string_view symbol,
                                      std::string_view parameter, bool required);

}  // namespace pdfkit::api

#endif  // PDFKIT_API_ARGUMENTS_HPP
