// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Error is the one exception type of the core. It carries a code and the three
// message parts every failure must state (what failed, what was expected, how
// to fix it), and its constructor refuses an empty part, so an error the caller
// cannot act on cannot be built. The C ABI turns it into a status and a JSON
// error object (src/api/guarded.cpp).

#ifndef PDFKIT_PLATFORM_ERROR_HPP
#define PDFKIT_PLATFORM_ERROR_HPP

#include <cstdint>
#include <exception>
#include <string>
#include <string_view>

namespace pdfkit::platform {

// The refusal codes are those of docs/DESIGN.md chapter 19; self_check is the
// write refusal of chapter 18; the last three are the core's own.
enum class ErrorCode : std::uint8_t {
  not_pdf,
  no_page,
  password_required,
  unsupported_security_handler,
  limit_exceeded,
  page_ceiling,
  self_check,
  invalid_argument,
  not_implemented,
  internal,
};

// The stable name of a code, as it appears in the JSON error object.
[[nodiscard]] std::string_view error_code_name(ErrorCode code) noexcept;

// Whether the code refuses the input document (the PDFKIT_E_REFUSED status).
[[nodiscard]] bool is_refusal(ErrorCode code) noexcept;

class Error : public std::exception {
 public:
  // Throws std::logic_error when a message part is empty.
  Error(ErrorCode code, std::string failed, std::string expected, std::string fix);

  [[nodiscard]] ErrorCode code() const noexcept { return code_; }
  [[nodiscard]] const std::string& failed() const noexcept { return failed_; }
  [[nodiscard]] const std::string& expected() const noexcept { return expected_; }
  [[nodiscard]] const std::string& fix() const noexcept { return fix_; }
  [[nodiscard]] const char* what() const noexcept override { return failed_.c_str(); }

 private:
  ErrorCode code_;
  std::string failed_;
  std::string expected_;
  std::string fix_;
};

}  // namespace pdfkit::platform

#endif  // PDFKIT_PLATFORM_ERROR_HPP
