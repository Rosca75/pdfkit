// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The key derivation and decryption of the standard security handler. The
// algorithm numbers are those of ISO 32000-1 section 7.6.3 and ISO 32000-2
// section 7.6.4 (read in the design record's sources); the behaviour follows
// pdf.js's crypto.js (read) where the specification leaves a choice: a bad
// AES pad keeps the bytes, AES data shorter than one block decrypts to
// nothing, and a crypt filter /Length is read as bits when above 32 and as
// bytes otherwise.

#include "syntax/security_handler.hpp"

#include "platform/crypto/aes.hpp"
#include "platform/crypto/md5.hpp"
#include "platform/crypto/rc4.hpp"
#include "platform/crypto/sha2.hpp"
#include "platform/error.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <utility>

namespace pdfkit::syntax {

namespace {

using platform::crypto::Aes;
using platform::crypto::Md5;
using platform::crypto::Rc4;
using platform::crypto::Sha2;

// The padding string of ISO 32000-1 section 7.6.3.3, algorithm 2 step a (read).
constexpr std::array<unsigned char, 32> padding_bytes{
    0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41, 0x64, 0x00, 0x4E, 0x56, 0xFF, 0xFA, 0x01, 0x08,
    0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68, 0x3E, 0x80, 0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A};

std::string padding() {
  return {padding_bytes.begin(), padding_bytes.end()};
}

platform::ByteSpan span_of(const std::string& text) {
  return platform::as_bytes(text);
}

template <typename Bytes>
std::string text_of(const Bytes& bytes) {
  std::string out;
  out.reserve(bytes.size());
  for (const std::byte value : bytes) {
    out.push_back(static_cast<char>(value));
  }
  return out;
}

std::string md5(const std::string& data) {
  return text_of(Md5::digest(span_of(data)));
}

std::string rc4(const std::string& key, const std::string& data) {
  return text_of(Rc4::transform(span_of(key), span_of(data)));
}

std::string xor_key(const std::string& key, unsigned value) {
  std::string out = key;
  for (char& byte : out) {
    byte = static_cast<char>(static_cast<unsigned char>(byte) ^ value);
  }
  return out;
}

[[noreturn]] void refuse_unsupported(const std::string& what) {
  throw platform::Error(platform::ErrorCode::unsupported_security_handler,
                        "the document is encrypted with " + what + ", which pdfkit does not read",
                        "an unencrypted document, or the standard security handler of "
                        "revisions 2 to 6",
                        "remove the protection in the producing application and save the file "
                        "again");
}

[[noreturn]] void refuse_password(int revision) {
  throw platform::Error(platform::ErrorCode::password_required,
                        "the document needs a user password to open (standard security "
                        "handler, revision " +
                            std::to_string(revision) + ")",
                        "a document that opens with an empty user password",
                        "remove the password in the PDF application that protects it, then "
                        "save an unprotected copy");
}

[[noreturn]] void refuse_key_check(int revision) {
  throw platform::Error(platform::ErrorCode::password_required,
                        "the file key the empty user password yields does not check against the "
                        "/Perms entry (standard security handler, revision " +
                            std::to_string(revision) +
                            "), so the document's strings and streams cannot be decrypted",
                        "a document that opens with an empty user password",
                        "open the file in the PDF application that protects it and save an "
                        "unprotected copy");
}

// The hash of algorithm 2.B (ISO 32000-2 section 7.6.4.3.4) for revision 6,
// plain SHA-256 for revision 5 (algorithm 2.A).
std::string hash_r6(int revision, const std::string& password, const std::string& salt,
                    const std::string& user_key) {
  std::string k = text_of(Sha2::digest(Sha2::Variant::sha256, span_of(password + salt + user_key)));
  if (revision == 5) {
    return k;
  }
  std::string block;
  std::string k1;
  for (unsigned round = 1;; ++round) {
    block = password;
    block += k;
    block += user_key;
    k1.clear();
    for (int i = 0; i < 64; ++i) {
      k1 += block;
    }
    const std::string e =
        text_of(Aes::cbc_encrypt(span_of(k.substr(0, 16)), span_of(k.substr(16, 16)), span_of(k1)));
    unsigned sum = 0;
    for (std::size_t i = 0; i < 16; ++i) {
      sum += static_cast<unsigned char>(e[i]);
    }
    const Sha2::Variant variant = sum % 3 == 0   ? Sha2::Variant::sha256
                                  : sum % 3 == 1 ? Sha2::Variant::sha384
                                                 : Sha2::Variant::sha512;
    k = text_of(Sha2::digest(variant, span_of(e)));
    // At least 64 rounds, then until the last byte of E is not greater than
    // the round number minus 32 (pdf.js's loop, read).
    if (round >= 64 && static_cast<unsigned char>(e.back()) <= round - 32) {
      break;
    }
  }
  return k.substr(0, 32);
}

std::string string_entry(const Dictionary& dictionary, std::string_view key) {
  const Object::String* value = dictionary.get(key).string();
  return value != nullptr ? value->bytes : std::string();
}

}  // namespace

SecurityHandler SecurityHandler::open(const Dictionary& encrypt, const std::string& id0) {
  const std::string* filter = encrypt.get("Filter").name();
  if (filter == nullptr || *filter != "Standard") {
    refuse_unsupported("the security handler /" + (filter != nullptr ? *filter : std::string("?")));
  }
  const int version = static_cast<int>(encrypt.get("V").integer().value_or(0));
  const auto revision_value = encrypt.get("R").integer();
  const int revision = static_cast<int>(revision_value.value_or(0));
  if (!revision_value || revision < 2 || revision > 6) {
    refuse_unsupported("the standard security handler of revision " +
                       (revision_value ? std::to_string(*revision_value) : std::string("(none)")));
  }
  if (version != 1 && version != 2 && version != 4 && version != 5) {
    refuse_unsupported("the standard security handler of version " + std::to_string(version) +
                       " (revision " + std::to_string(revision) + ")");
  }

  SecurityHandler handler;
  Description& description = handler.description_;
  description.version = version;
  description.revision = revision;
  const auto metadata = encrypt.get("EncryptMetadata").boolean();
  description.encrypt_metadata = !metadata.has_value() || *metadata;

  // The crypt filters (ISO 32000-1 section 7.6.5).
  const auto method_of = [&](const std::string& name) -> Method {
    if (name == "Identity") {
      return Method::identity;
    }
    const Dictionary* filters = encrypt.get("CF").dictionary();
    const Dictionary* entry = filters != nullptr ? filters->get(name).dictionary() : nullptr;
    if (entry == nullptr) {
      refuse_unsupported("the crypt filter /" + name + ", which its /CF does not define");
    }
    const std::string* cfm = entry->get("CFM").name();
    if (cfm == nullptr || *cfm == "None") {
      return Method::identity;
    }
    if (*cfm == "V2") {
      return Method::rc4;
    }
    if (*cfm == "AESV2") {
      return Method::aes128;
    }
    if (*cfm == "AESV3") {
      return Method::aes256;
    }
    refuse_unsupported("the crypt filter method /" + *cfm);
  };
  if (version >= 4) {
    const std::string* stream_filter = encrypt.get("StmF").name();
    const std::string* string_filter = encrypt.get("StrF").name();
    handler.stream_method_ = method_of(stream_filter != nullptr ? *stream_filter : "Identity");
    handler.string_method_ = method_of(string_filter != nullptr ? *string_filter : "Identity");
    if (const Dictionary* filters = encrypt.get("CF").dictionary()) {
      for (const auto& [name, value] : filters->entries()) {
        if (value.dictionary() != nullptr) {
          handler.crypt_filters_.emplace_back(name, method_of(name));
        }
      }
    }
  } else {
    handler.stream_method_ = Method::rc4;
    handler.string_method_ = Method::rc4;
  }
  const Method main_method =
      handler.stream_method_ != Method::identity ? handler.stream_method_ : handler.string_method_;
  description.method = main_method == Method::rc4      ? "RC4"
                       : main_method == Method::aes128 ? "AESV2"
                       : main_method == Method::aes256 ? "AESV3"
                                                       : "Identity";

  // With /StmF and /StrF both /Identity nothing the document draws is
  // encrypted; a user password then guards only what /EFF names (embedded
  // files, never read here), so it is not checked (PDFium opens such files).
  if (version >= 4 && handler.stream_method_ == Method::identity &&
      handler.string_method_ == Method::identity) {
    description.key_bits = static_cast<int>(encrypt.get("Length").integer().value_or(0));
    return handler;
  }

  const std::string owner_entry = string_entry(encrypt, "O");
  const std::string user_entry = string_entry(encrypt, "U");

  if (revision >= 5) {
    // Algorithm 2.A with the empty password (SASLprep of "" is "").
    if (user_entry.size() < 48) {
      refuse_unsupported("a revision " + std::to_string(revision) + " handler whose /U is short");
    }
    const std::string validation_salt = user_entry.substr(32, 8);
    const std::string key_salt = user_entry.substr(40, 8);
    if (hash_r6(revision, "", validation_salt, "") != user_entry.substr(0, 32)) {
      refuse_password(revision);
    }
    const std::string intermediate = hash_r6(revision, "", key_salt, "");
    const std::string ue = string_entry(encrypt, "UE");
    if (ue.size() < 32) {
      refuse_unsupported("a revision " + std::to_string(revision) + " handler whose /UE is short");
    }
    const std::string zero_iv(16, '\0');
    handler.file_key_ = text_of(
        Aes::cbc_decrypt(span_of(intermediate), span_of(zero_iv), span_of(ue.substr(0, 32))));
    description.key_bits = 256;
    // /Perms: AES-256 of one block under the file key holds the permissions
    // in bytes 0 to 3 and "adb" in bytes 9 to 11 (algorithm 13). Without
    // "adb" the key decrypted from /UE is wrong and every string and stream
    // would decrypt to noise, so the file is refused as PDFium refuses it; a
    // permissions mismatch or a missing /Perms is only a warning.
    const std::string perms = string_entry(encrypt, "Perms");
    if (perms.size() >= 16) {
      const std::string clear = text_of(Aes::cbc_decrypt(
          span_of(handler.file_key_), span_of(zero_iv), span_of(perms.substr(0, 16))));
      if (clear.substr(9, 3) != "adb") {
        refuse_key_check(revision);
      }
      const auto permissions = static_cast<std::uint32_t>(encrypt.get("P").integer().value_or(0));
      std::uint32_t held = 0;
      for (unsigned i = 0; i < 4; ++i) {
        held |= static_cast<std::uint32_t>(static_cast<unsigned char>(clear[i])) << (8U * i);
      }
      handler.perms_mismatch_ = held != permissions;
    } else {
      handler.perms_mismatch_ = true;
    }
    return handler;
  }

  // Algorithm 2: the file key from the padded empty password.
  std::int64_t length_bits = encrypt.get("Length").integer().value_or(40);
  if (revision == 2) {
    length_bits = 40;
  }
  if (length_bits < 40 || length_bits > 128 || length_bits % 8 != 0) {
    length_bits = std::clamp<std::int64_t>(length_bits / 8 * 8, 40, 128);
  }
  const auto key_length = static_cast<std::size_t>(length_bits / 8);
  description.key_bits = static_cast<int>(length_bits);
  const auto permissions = static_cast<std::uint32_t>(encrypt.get("P").integer().value_or(0));
  std::string input = padding();
  input += owner_entry.substr(0, 32);
  for (unsigned shift = 0; shift < 32; shift += 8) {
    input.push_back(static_cast<char>((permissions >> shift) & 0xFFU));
  }
  input += id0;
  if (revision >= 4 && !description.encrypt_metadata) {
    input += std::string(4, '\xFF');
  }
  std::string key = md5(input);
  if (revision >= 3) {
    for (int i = 0; i < 50; ++i) {
      key = md5(key.substr(0, key_length));
    }
  }
  handler.file_key_ = key.substr(0, key_length);

  // Algorithms 4 and 5: the empty password is right when /U matches.
  if (revision == 2) {
    if (rc4(handler.file_key_, padding()) != user_entry.substr(0, 32)) {
      refuse_password(revision);
    }
  } else {
    std::string value = rc4(handler.file_key_, md5(padding() + id0));
    for (unsigned i = 1; i <= 19; ++i) {
      value = rc4(xor_key(handler.file_key_, i), value);
    }
    if (value.substr(0, 16) != user_entry.substr(0, 16)) {
      refuse_password(revision);
    }
  }
  return handler;
}

std::vector<std::byte> SecurityHandler::apply(Method method, Object::Reference owner,
                                              platform::ByteSpan data) const {
  if (method == Method::identity) {
    return {data.begin(), data.end()};
  }
  std::string key = file_key_;
  if (method != Method::aes256) {
    // Algorithm 1: the per-object key.
    for (unsigned shift = 0; shift < 24; shift += 8) {
      key.push_back(static_cast<char>((owner.number >> shift) & 0xFFU));
    }
    key.push_back(static_cast<char>(owner.generation & 0xFFU));
    key.push_back(static_cast<char>((owner.generation >> 8U) & 0xFFU));
    if (method == Method::aes128) {
      key += "sAlT";
    }
    key = md5(key).substr(0, std::min<std::size_t>(file_key_.size() + 5, 16));
  }
  if (method == Method::rc4) {
    return Rc4::transform(span_of(key), data);
  }
  if (data.size() < 2 * Aes::block_size) {
    // An IV alone, or less, holds no data (pdf.js reads it so).
    return {};
  }
  const std::vector<std::byte> clear =
      Aes::cbc_decrypt(span_of(key), data.first(Aes::block_size), data.subspan(Aes::block_size));
  return Aes::pkcs7_unpad(clear).bytes;
}

std::string SecurityHandler::decrypt_string(Object::Reference owner,
                                            const std::string& bytes) const {
  return text_of(apply(string_method_, owner, span_of(bytes)));
}

std::vector<std::byte> SecurityHandler::decrypt_stream(Object::Reference owner,
                                                       const Dictionary& dictionary,
                                                       platform::ByteSpan data) const {
  if (dictionary.get("Type").is_name("XRef")) {
    return {data.begin(), data.end()};
  }
  if (!description_.encrypt_metadata && dictionary.get("Type").is_name("Metadata")) {
    return {data.begin(), data.end()};
  }
  // A stream whose first filter is /Crypt names its own crypt filter in its
  // /DecodeParms /Name, defaulting to /Identity (ISO 32000-1 section 7.4.10).
  const Object& filter = dictionary.get("Filter");
  const Object::Array* filters = filter.array();
  const bool own_crypt = filter.is_name("Crypt") || (filters != nullptr && !filters->empty() &&
                                                     filters->front().is_name("Crypt"));
  if (own_crypt) {
    const Object& parameters = dictionary.get("DecodeParms");
    const Object& first = parameters.array() != nullptr && !parameters.array()->empty()
                              ? parameters.array()->front()
                              : parameters;
    const Dictionary* crypt = first.dictionary();
    const std::string* name = crypt != nullptr ? crypt->get("Name").name() : nullptr;
    if (name == nullptr || *name == "Identity") {
      return {data.begin(), data.end()};
    }
    for (const auto& [defined, method] : crypt_filters_) {
      if (defined == *name) {
        return apply(method, owner, data);
      }
    }
  }
  return apply(stream_method_, owner, data);
}

}  // namespace pdfkit::syntax
