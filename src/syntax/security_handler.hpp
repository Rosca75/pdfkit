// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// SecurityHandler is the standard security handler for reading, docs/DESIGN.md
// chapter 2, with the empty user password only: revisions 2 to 4 (RC4 of 40
// to 128 bits and AESV2; algorithms 1, 2, 4 and 5 of ISO 32000-1 section
// 7.6.3) and revisions 5 and 6 (AESV3; algorithm 2.A and the revision-6 hash
// of algorithm 2.B, ISO 32000-2 section 7.6.4), with the crypt filters /StdCF
// and /Identity, /EncryptMetadata false and the per-object keys. pdf.js's
// crypto.js (read in the design record) is the reference behaviour. A
// non-empty user password is the refusal password_required; a handler other
// than /Standard or a revision outside 2 to 6 is unsupported_security_handler.
// Every file pdfkit writes is unencrypted, so this handler only decrypts.

#ifndef PDFKIT_SYNTAX_SECURITY_HANDLER_HPP
#define PDFKIT_SYNTAX_SECURITY_HANDLER_HPP

#include "platform/byte_span.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/object.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pdfkit::syntax {

class SecurityHandler {
 public:
  enum class Method : std::uint8_t { identity, rc4, aes128, aes256 };

  // What the open report and inspect say about the encryption.
  struct Description {
    int version = 0;
    int revision = 0;
    // "RC4", "AESV2", "AESV3" or "Identity", the method strings use.
    std::string method;
    int key_bits = 0;
    bool encrypt_metadata = true;
  };

  // Builds the handler from the /Encrypt dictionary (resolved, its strings
  // left encrypted as the file holds them) and the first element of the
  // trailer's /ID. Throws platform::Error password_required or
  // unsupported_security_handler.
  [[nodiscard]] static SecurityHandler open(const Dictionary& encrypt, const std::string& id0);

  // The decrypted bytes of a string held by owner.
  [[nodiscard]] std::string decrypt_string(Object::Reference owner, const std::string& bytes) const;
  // The decrypted data of a stream held by owner whose dictionary is
  // dictionary: its own /Crypt filter, /Type /XRef and an unencrypted
  // /Metadata stream are honoured.
  [[nodiscard]] std::vector<std::byte> decrypt_stream(Object::Reference owner,
                                                      const Dictionary& dictionary,
                                                      platform::ByteSpan data) const;

  [[nodiscard]] const Description& description() const noexcept { return description_; }
  // Revisions 5 and 6: /Perms did not decrypt to "adb" under the file key,
  // which is a warning, never a refusal.
  [[nodiscard]] bool perms_mismatch() const noexcept { return perms_mismatch_; }
  [[nodiscard]] const std::string& file_key() const noexcept { return file_key_; }

 private:
  SecurityHandler() = default;

  [[nodiscard]] std::vector<std::byte> apply(Method method, Object::Reference owner,
                                             platform::ByteSpan data) const;

  Description description_;
  std::string file_key_;
  Method string_method_ = Method::identity;
  Method stream_method_ = Method::identity;
  // The crypt filters of /CF by name, for a stream naming its own.
  std::vector<std::pair<std::string, Method>> crypt_filters_;
  bool perms_mismatch_ = false;
};

}  // namespace pdfkit::syntax

#endif  // PDFKIT_SYNTAX_SECURITY_HANDLER_HPP
