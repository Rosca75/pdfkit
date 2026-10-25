// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of SHA-256, SHA-384 and SHA-512 against the published examples of
// FIPS 180-4 and RFC 6234, against digests of messages that straddle the
// padding boundaries, and of the incremental interface against the one-shot
// one: the revision 6 key check hashes in many pieces, so the splitting must
// never change the digest.

#include "platform/crypto/sha2.hpp"

#include "platform/byte_span.hpp"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace {

using pdfkit::platform::as_bytes;
using pdfkit::platform::ByteSpan;
using pdfkit::platform::crypto::Sha2;
using Variant = Sha2::Variant;

std::string hex(ByteSpan bytes) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string out;
  for (const std::byte b : bytes) {
    const auto v = std::to_integer<unsigned>(b);
    out.push_back(digits[v >> 4U]);
    out.push_back(digits[v & 15U]);
  }
  return out;
}

std::string sha_hex(Variant variant, std::string_view text) {
  const auto digest = Sha2::digest(variant, as_bytes(text));
  return hex(digest);
}

struct Example {
  std::string_view name;
  Variant variant;
  std::string_view message;
  std::string_view digest;
};

// The 448-bit and 896-bit messages and their digests are the examples of
// https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/examples/
// SHA256.pdf, SHA384.pdf and SHA512.pdf (read); "abc" is in the same files. The empty message
// and the 448-bit message under SHA-384 and SHA-512 are not in those files: their digests are
// computed with Python's hashlib (the empty digests are the well-known ones).
constexpr std::string_view abc = "abc";
constexpr std::string_view bits448 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
constexpr std::string_view bits896 =
    "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopq"
    "rsmnopqrstnopqrstu";

constexpr std::array<Example, 11> examples{{
    {"sha256 empty", Variant::sha256, "",
     "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
    {"sha256 abc", Variant::sha256, abc,
     "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
    {"sha256 448 bits", Variant::sha256, bits448,
     "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
    {"sha384 empty", Variant::sha384, "",
     "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898"
     "b95b"},
    {"sha384 abc", Variant::sha384, abc,
     "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c8"
     "25a7"},
    {"sha384 896 bits", Variant::sha384, bits896,
     "09330c33f71147e83d192fc782cd1b4753111b173b3b05d22fa08086e3b0f712fcc7c71a557e2db966c3e9fa9174"
     "6039"},
    {"sha384 448 bits", Variant::sha384, bits448,
     "3391fdddfc8dc7393707a65b1b4709397cf8b1d162af05abfe8f450de5f36bc6b0455a8520bc4e6f5fe95b1fe3c8"
     "452b"},
    {"sha512 empty", Variant::sha512, "",
     "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877e"
     "ec2f63b931bd47417a81a538327af927da3e"},
    {"sha512 abc", Variant::sha512, abc,
     "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3fe"
     "ebbd454d4423643ce80e2a9ac94fa54ca49f"},
    {"sha512 896 bits", Variant::sha512, bits896,
     "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018501d289e4900f7e4331b99dec4b5"
     "433ac7d329eeb6dd26545e96e55b874be909"},
    {"sha512 448 bits", Variant::sha512, bits448,
     "204a8fc6dda82f0a0ced7beb8e08a41657c16ef468b228a8279be331a703c33596fd15c13b1b07f9aa1d3bea5778"
     "9ca031ad85c7a71dd70354ec631238ca3445"},
}};

TEST_CASE("sha2 matches the published examples of every variant") {
  for (const Example& e : examples) {
    INFO(e.name);
    CHECK(sha_hex(e.variant, e.message) == e.digest);
  }
}

TEST_CASE("sha2 matches the digests of one million 'a' in FIPS 180-2 and RFC 6234") {
  // FIPS 180-2 appendix B.3, C.3 and D.3, https://csrc.nist.gov/files/pubs/fips/180-2/final/docs/
  // fips180-2.pdf (read), and RFC 6234 section 8.5, https://www.rfc-editor.org/rfc/rfc6234.txt.
  const std::string million(1000000, 'a');
  CHECK(sha_hex(Variant::sha256, million) ==
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  CHECK(
      sha_hex(Variant::sha384, million) ==
      "9d0e1809716474cb086e834e310a4a1ced149e9c00f248527972cec5704c2a5b07b8b3dc38ecc4ebae97ddd87f3d"
      "8985");
  CHECK(
      sha_hex(Variant::sha512, million) ==
      "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973ebde0ff244877ea60a4cb0432ce577"
      "c31beb009c5c2c49aa2e4eadb217ad8cc09b");
}

TEST_CASE("sha2 digests have the length of their variant") {
  CHECK(Sha2::digest(Variant::sha256, ByteSpan{}).size() == 32);
  CHECK(Sha2::digest(Variant::sha384, ByteSpan{}).size() == 48);
  CHECK(Sha2::digest(Variant::sha512, ByteSpan{}).size() == 64);
}

struct LengthVector {
  std::size_t length;
  std::string_view digest;
};

// Digests of 'a' repeated length times, for lengths on both sides of the padding boundaries
// (55/56/64 bytes for SHA-256, 111/112/128 bytes for SHA-384 and SHA-512). Computed with Python's
// hashlib; the algorithms' own vectors are above.
constexpr std::array<LengthVector, 12> sha256_repeated_a{{
    {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
    {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
    {57, "f13b2d724659eb3bf47f2dd6af1accc87b81f09f59f2b75e5c0bed6589dfe8c6"},
    {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
    {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
    {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
    {111, "6374f73208854473827f6f6a3f43b1f53eaa3b82c21c1a6d69a2110b2a79baad"},
    {112, "f54353008a2553262ecdc4a34749563ba0950e8b0fc8652780b0a614b99683c1"},
    {113, "ba02731ae695aae5cd49b49d84330b63995733eb22102aca755f0179b1e0e20f"},
    {127, "c57e9278af78fa3cab38667bef4ce29d783787a2f731d4e12200270f0c32320a"},
    {128, "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e"},
    {129, "c12cb024a2e5551cca0e08fce8f1c5e314555cc3fef6329ee994a3db752166ae"},
}};
constexpr std::array<LengthVector, 12> sha384_repeated_a{{
    {55,
     "5d91ac7e74e62b5c728904b40f10784d66b7af9cb6302123e48c92f0432ceb8d2a92c02de77dcb29ed75c4b42bde4"
     "6f4"},
    {56,
     "8a8d9649ea04e993a6ca7135af7e3392cc5fca84f8531cac7aa3feed4eb98f55dcbe0f3284b61c6f35f98b02cc644"
     "b4c"},
    {57,
     "5e872b76ee122b6d638d95a087344fded7836f53a3ff200aea18e3c9e952b2410ffb78b8b346dbd4d6bfe855d3fb7"
     "a91"},
    {63,
     "7e7f097e95b52bb8f53383450ecaf9868187c130981730c03b6d573adfc0b991e365244e5b4bfa082bfb43c517d37"
     "120"},
    {64,
     "2e404b9339da795776e510d96930b3be2904c500395b8cb7413334b82d4dec413b4b8113045a05bbbcff846f02742"
     "3f6"},
    {65,
     "a2b797de45aa1d20594e2ea0cb1801d6dd7ffa3a58281c37dde04029f1c98157bb6eb005e90dd76b79a1d6ddd52dd"
     "58d"},
    {111,
     "3c37955051cb5c3026f94d551d5b5e2ac38d572ae4e07172085fed81f8466b8f90dc23a8ffcdea0b8d8e58e8fdacc"
     "80a"},
    {112,
     "187d4e07cb306103c69967bf544d0dfbe9042577599c73c330abc0cb64c61236d5ed565ee19119d8c31779a38f791"
     "fcd"},
    {113,
     "1d6bed01626682961b50da078a6b1da707c1da0c8a0a3226f159235bd45ed724a0622fa6f39fd70007a6c72a5cda4"
     "3ae"},
    {127,
     "9bd06b1763c2cf7aef40e795dc65bc96d59c41b537f3ad72ebdefd485476b5717c1aeb37c327fe9c1831b12b9efd0"
     "8ae"},
    {128,
     "edb12730a366098b3b2beac75a3bef1b0969b15c48e2163c23d96994f8d1bef760c7e27f3c464d3829f56c0d53808"
     "b0b"},
    {129,
     "39b6f5a7b0e781dbc419f72e49b30eaac10f2c98c4403bc610da31067fd1b48f324138c8615d2b496d08d73d5e865"
     "326"},
}};
constexpr std::array<LengthVector, 12> sha512_repeated_a{{
    {55,
     "b0220c772cbf6c1822e2cb38a437d0e1d58772417a4bbb21c961364f8b6143e05aa6316dca8d1d7b19e1644841907"
     "6395f6086cb55101fbd6d5497b148e1745f"},
    {56,
     "962b64aae357d2a4fee3ded8b539bdc9d325081822b0bfc55583133aab44f18bafe11d72a7ae16c79ce2ba620ae22"
     "42d5144809161945f1367f41b3972e26e04"},
    {57,
     "d3115798e872fc1ca6b276368e8ea0926daec6ab1f8f08297e4348ff5f5fe4c6e5205413271babafd4929b070754b"
     "c5800e5db44790666ec4e2f6ac52a17e163"},
    {63,
     "c1b0f5c6d3b03dfe4a2602e67242f54e344090b66e01100a469b129f583f016c7e27dddeaa438393dcc7ec54b0b57"
     "c9ba7af007f9b56db5f6fb677d972a31362"},
    {64,
     "01d35c10c6c38c2dcf48f7eebb3235fb5ad74a65ec4cd016e2354c637a8fb49b695ef3c1d6f7ae4cd74d78cc9c9bc"
     "ac9d4f23a73019998a7f73038a5c9b2dbde"},
    {65,
     "b83086cd8494e55708ad7ecd82dfb4bca1bda61ecbb7caf0c68967902e709345e5d8305eb7ac0d588afc6cbb75161"
     "aa9c8c7e0ea986bd833dafe5e1ccd37345a"},
    {111,
     "fa9121c7b32b9e01733d034cfc78cbf67f926c7ed83e82200ef86818196921760b4beff48404df811b95382827446"
     "1673c68d04e297b0eb7b2b4d60fc6b566a2"},
    {112,
     "c01d080efd492776a1c43bd23dd99d0a2e626d481e16782e75d54c2503b5dc32bd05f0f1ba33e568b88fd2d970929"
     "b719ecbb152f58f130a407c8830604b70ca"},
    {113,
     "55ddd8ac210a6e18ba1ee055af84c966e0dbff091c43580ae1be703bdb85da31acf6948cf5bd90c55a20e5450f22f"
     "b89bd8d0085e39f85a86cc46abbca75e24d"},
    {127,
     "828613968b501dc00a97e08c73b118aa8876c26b8aac93df128502ab360f91bab50a51e088769a5c1eff4782ace14"
     "7dce3642554199876374291f5d921629502"},
    {128,
     "b73d1929aa615934e61a871596b3f3b33359f42b8175602e89f7e06e5f658a243667807ed300314b95cacdd579f3e"
     "33abdfbe351909519a846d465c59582f321"},
    {129,
     "4f681e0bd53cda4b5a2041cc8a06f2eabde44fb16c951fbd5b87702f07aeab611565b19c47fde30587177ebb852e3"
     "971bbd8d3fd30da18d71037dfbd98420429"},
}};

template <std::size_t N>
void check_repeated_a(Variant variant, const std::array<LengthVector, N>& table) {
  for (const LengthVector& v : table) {
    INFO("length: " << v.length);
    const std::string message(v.length, 'a');
    CHECK(sha_hex(variant, message) == v.digest);
  }
}

TEST_CASE("sha2 pads messages at the block boundaries") {
  check_repeated_a(Variant::sha256, sha256_repeated_a);
  check_repeated_a(Variant::sha384, sha384_repeated_a);
  check_repeated_a(Variant::sha512, sha512_repeated_a);
}

TEST_CASE("sha2 gives the same digest however the message is split") {
  const std::string message(400, 'q');
  const ByteSpan all = as_bytes(message);
  for (const Variant variant : {Variant::sha256, Variant::sha384, Variant::sha512}) {
    const std::string one_shot = sha_hex(variant, message);
    for (const std::size_t first :
         {0U, 1U, 55U, 56U, 63U, 64U, 65U, 111U, 112U, 127U, 128U, 129U, 255U, 256U, 399U, 400U}) {
      for (const std::size_t second : {0U, 1U, 7U, 64U, 100U, 129U}) {
        const std::size_t middle = std::min(first + second, all.size());
        Sha2 sha(variant);
        sha.update(all.first(first));
        sha.update(all.subspan(first, middle - first));
        sha.update(all.subspan(middle));
        INFO("split at " << first << " and " << middle);
        CHECK(hex(sha.finish()) == one_shot);
      }
    }
  }
}

TEST_CASE("sha2 fed one byte at a time equals the one-shot digest") {
  Sha2 sha(Variant::sha256);
  for (const char c : bits448) {
    const std::array<std::byte, 1> one{static_cast<std::byte>(c)};
    sha.update(one);
  }
  CHECK(hex(sha.finish()) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("sha2 ignores an empty update") {
  Sha2 sha(Variant::sha512);
  sha.update(ByteSpan{});
  sha.update(as_bytes(abc));
  sha.update(ByteSpan{});
  CHECK(
      hex(sha.finish()) ==
      "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3fe"
      "ebbd454d4423643ce80e2a9ac94fa54ca49f");
}

}  // namespace
