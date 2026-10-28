// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// PdfEncryptor is the fixture writer's standard security handler: from a
// revision, a key length and two passwords it computes the /Encrypt
// dictionary's /O, /U, /OE, /UE and /Perms and encrypts each object's strings
// and streams. It is written in the test tree from the algorithms of ISO
// 32000-1 section 7.6.3 (algorithms 1, 2, 3, 4, 5) and ISO 32000-2 section
// 7.6.4 (algorithms 2.A, 2.B, 8, 9, 10), the one place a second copy of the
// handler is allowed, because a reader tested only against its own writer
// proves nothing. Only the block ciphers and hashes are shared with the
// library, and those are tested against their published vectors.

#ifndef PDFKIT_TESTS_SUPPORT_PDF_ENCRYPTOR_HPP
#define PDFKIT_TESTS_SUPPORT_PDF_ENCRYPTOR_HPP

#include <cstdint>
#include <string>

namespace pdfkit::test {

class PdfEncryptor {
 public:
  enum class Method : std::uint8_t { rc4, aes };

  struct Spec {
    // 2 to 6. Revision 2 is RC4 with a 40-bit key, 3 RC4 with key_bits, 4 RC4 or
    // AES-128 through a crypt filter, 5 and 6 AES-256.
    int revision = 4;
    int key_bits = 128;
    Method method = Method::aes;
    bool encrypt_metadata = true;
    std::string user_password;
    std::string owner_password = "owner";
    std::int32_t permissions = -3904;
    // Revisions 4 to 6: the crypt filter name /StmF and /StrF use;
    // "Identity" leaves the data in clear.
    std::string stream_filter = "StdCF";
    std::string string_filter = "StdCF";
    // Revisions 5 and 6: write a /Perms that does not decrypt to "adb" (the
    // file key fails its check), or one whose permissions differ from /P.
    bool corrupt_perms = false;
    bool perms_other_permissions = false;
    // Entries written as they are at the end of the /Encrypt dictionary,
    // whose strings are never encrypted (a vendor key, for instance).
    std::string extra_entries;
  };

  // id0 is the first element of the trailer's /ID.
  PdfEncryptor(Spec spec, std::string id0);

  // The body of the /Encrypt dictionary, "<< /Filter /Standard ... >>".
  [[nodiscard]] std::string dictionary() const;

  // The encrypted form of a string or of a stream's data held by object
  // number/generation. A stream is_metadata when its dictionary is /Type
  // /Metadata (left clear when /EncryptMetadata is false).
  [[nodiscard]] std::string encrypt_string(std::uint32_t number, std::uint32_t generation,
                                           const std::string& bytes) const;
  [[nodiscard]] std::string encrypt_stream(std::uint32_t number, std::uint32_t generation,
                                           const std::string& bytes, bool is_metadata) const;

  [[nodiscard]] const Spec& spec() const noexcept { return spec_; }

 private:
  [[nodiscard]] std::string encrypt(std::uint32_t number, std::uint32_t generation,
                                    const std::string& bytes, bool aes) const;
  void compute_r2_to_r4();
  void compute_r5_r6();

  Spec spec_;
  std::string id0_;
  std::string file_key_;
  std::string o_;
  std::string u_;
  std::string oe_;
  std::string ue_;
  std::string perms_;
};

}  // namespace pdfkit::test

#endif  // PDFKIT_TESTS_SUPPORT_PDF_ENCRYPTOR_HPP
