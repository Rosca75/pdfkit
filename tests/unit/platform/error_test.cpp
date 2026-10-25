// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of platform::Error: every code has a stable name, and an error with an
// empty message part cannot be built, so every failure the library reports
// tells the caller what failed, what was expected and how to fix it.

#include "platform/error.hpp"

#include <doctest.h>

#include <stdexcept>
#include <string>

namespace {

using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;

TEST_CASE("an error keeps its code and its three message parts") {
  const Error error(ErrorCode::limit_exceeded, "object 12 inflates past 512 MiB",
                    "a stream within the per-stream limit", "raise stream_bytes at open");
  CHECK(error.code() == ErrorCode::limit_exceeded);
  CHECK(error.failed() == "object 12 inflates past 512 MiB");
  CHECK(error.expected() == "a stream within the per-stream limit");
  CHECK(error.fix() == "raise stream_bytes at open");
  CHECK(std::string(error.what()) == "object 12 inflates past 512 MiB");
}

TEST_CASE("an error with an empty message part is refused") {
  CHECK_THROWS_AS(Error(ErrorCode::internal, "", "b", "c"), std::logic_error);
  CHECK_THROWS_AS(Error(ErrorCode::internal, "a", "", "c"), std::logic_error);
  CHECK_THROWS_AS(Error(ErrorCode::internal, "a", "b", ""), std::logic_error);
}

TEST_CASE("every code has the name the design's refusal table uses") {
  CHECK(pdfkit::platform::error_code_name(ErrorCode::not_pdf) == "not_pdf");
  CHECK(pdfkit::platform::error_code_name(ErrorCode::no_page) == "no_page");
  CHECK(pdfkit::platform::error_code_name(ErrorCode::password_required) == "password_required");
  CHECK(pdfkit::platform::error_code_name(ErrorCode::unsupported_security_handler) ==
        "unsupported_security_handler");
  CHECK(pdfkit::platform::error_code_name(ErrorCode::limit_exceeded) == "limit_exceeded");
  CHECK(pdfkit::platform::error_code_name(ErrorCode::page_ceiling) == "page_ceiling");
  CHECK(pdfkit::platform::error_code_name(ErrorCode::undecodable_image) == "undecodable_image");
  CHECK(pdfkit::platform::error_code_name(ErrorCode::self_check) == "self_check");
  CHECK(pdfkit::platform::error_code_name(ErrorCode::invalid_argument) == "invalid_argument");
  CHECK(pdfkit::platform::error_code_name(ErrorCode::not_implemented) == "not_implemented");
  CHECK(pdfkit::platform::error_code_name(ErrorCode::internal) == "internal");
}

TEST_CASE("the refusal codes are exactly the input refusals") {
  CHECK(pdfkit::platform::is_refusal(ErrorCode::not_pdf));
  CHECK(pdfkit::platform::is_refusal(ErrorCode::no_page));
  CHECK(pdfkit::platform::is_refusal(ErrorCode::password_required));
  CHECK(pdfkit::platform::is_refusal(ErrorCode::unsupported_security_handler));
  CHECK(pdfkit::platform::is_refusal(ErrorCode::limit_exceeded));
  CHECK(pdfkit::platform::is_refusal(ErrorCode::page_ceiling));
  CHECK(pdfkit::platform::is_refusal(ErrorCode::undecodable_image));
  CHECK_FALSE(pdfkit::platform::is_refusal(ErrorCode::self_check));
  CHECK_FALSE(pdfkit::platform::is_refusal(ErrorCode::invalid_argument));
  CHECK_FALSE(pdfkit::platform::is_refusal(ErrorCode::not_implemented));
  CHECK_FALSE(pdfkit::platform::is_refusal(ErrorCode::internal));
}

}  // namespace
