// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The code names and the constructor check of platform::Error.

#include "platform/error.hpp"

#include <stdexcept>
#include <utility>

namespace pdfkit::platform {

std::string_view error_code_name(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::not_pdf:
      return "not_pdf";
    case ErrorCode::no_page:
      return "no_page";
    case ErrorCode::password_required:
      return "password_required";
    case ErrorCode::unsupported_security_handler:
      return "unsupported_security_handler";
    case ErrorCode::limit_exceeded:
      return "limit_exceeded";
    case ErrorCode::page_ceiling:
      return "page_ceiling";
    case ErrorCode::undecodable_image:
      return "undecodable_image";
    case ErrorCode::self_check:
      return "self_check";
    case ErrorCode::invalid_argument:
      return "invalid_argument";
    case ErrorCode::not_implemented:
      return "not_implemented";
    case ErrorCode::internal:
      return "internal";
  }
  return "internal";
}

bool is_refusal(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::not_pdf:
    case ErrorCode::no_page:
    case ErrorCode::password_required:
    case ErrorCode::unsupported_security_handler:
    case ErrorCode::limit_exceeded:
    case ErrorCode::page_ceiling:
    case ErrorCode::undecodable_image:
      return true;
    case ErrorCode::self_check:
    case ErrorCode::invalid_argument:
    case ErrorCode::not_implemented:
    case ErrorCode::internal:
      return false;
  }
  return false;
}

Error::Error(ErrorCode code, std::string failed, std::string expected, std::string fix)
    : code_(code),
      failed_(std::move(failed)),
      expected_(std::move(expected)),
      fix_(std::move(fix)) {
  if (failed_.empty() || expected_.empty() || fix_.empty()) {
    throw std::logic_error(
        "platform::Error was built with an empty message part; every error states what failed, "
        "what was expected and how to fix it");
  }
}

}  // namespace pdfkit::platform
