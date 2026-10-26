// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of pdfkit_redact_images through the C ABI: the catalogue lists it as
// implemented; every refusal carries its code and the three fields (an
// unknown identity, a second treatment of one identity naming the first, a
// box or blur of an image that does not decode with remove offered, a caption
// outside the alphabet, a region without a rectangle, a page rectangle on a
// page that does not draw the image, malformed input); a refused call leaves
// the plan as it was, and a remove of the undecodable image is accepted.

#include "pdfkit/pdfkit.h"

#include "platform/json_reader.hpp"
#include "redact/support/image_redaction.hpp"
#include "support/pdf_builder.hpp"

#include <doctest.h>

#include <string>

namespace {

using pdfkit::platform::JsonReader;
using pdfkit::platform::JsonValue;

std::string take(pdfkit_buffer& buffer) {
  std::string text(reinterpret_cast<const char*>(buffer.data), buffer.size);
  pdfkit_buffer_free(&buffer);
  return text;
}

// Opens file and keeps the handles for several calls.
class Session {
 public:
  explicit Session(const std::string& file) {
    pdfkit_buffer out{nullptr, 0, nullptr};
    REQUIRE(pdfkit_open(reinterpret_cast<const std::uint8_t*>(file.data()), file.size(), nullptr,
                        &doc_, &out) == PDFKIT_OK);
    (void)take(out);
    REQUIRE(pdfkit_edit_new(doc_, &edit_, &out) == PDFKIT_OK);
    (void)take(out);
  }
  ~Session() {
    pdfkit_edit_free(edit_);
    pdfkit_close(doc_);
  }
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  pdfkit_status call(const std::string& json, std::string& answer) {
    pdfkit_buffer out{nullptr, 0, nullptr};
    const pdfkit_status status = pdfkit_redact_images(edit_, json.c_str(), &out);
    answer = take(out);
    return status;
  }

  std::string write() {
    pdfkit_buffer pdf{nullptr, 0, nullptr};
    pdfkit_buffer out{nullptr, 0, nullptr};
    REQUIRE(pdfkit_write(edit_, nullptr, &pdf, &out) == PDFKIT_OK);
    (void)take(out);
    return take(pdf);
  }

 private:
  pdfkit_document* doc_ = nullptr;
  pdfkit_edit* edit_ = nullptr;
};

void check_refusal(const std::string& answer, const std::string& code, const std::string& named) {
  INFO(answer);
  const JsonValue error = JsonReader::parse(answer);
  CHECK(error.find("code")->as_string() == code);
  CHECK(error.find("failed")->as_string().find(named) != std::string::npos);
  CHECK(!error.find("expected")->as_string().empty());
  CHECK(!error.find("fix")->as_string().empty());
}

std::string treatment(const std::string& identity, const std::string& rest) {
  return R"({"treatments": [{"identity": ")" + identity + R"(", )" + rest + "}]}";
}

}  // namespace

TEST_CASE("the catalogue lists pdfkit_redact_images as implemented") {
  pdfkit_buffer out{nullptr, 0, nullptr};
  REQUIRE(pdfkit_version(&out) == PDFKIT_OK);
  const JsonValue version = JsonReader::parse(take(out));
  bool found = false;
  for (const JsonValue& row : version.find("catalogue")->as_array()) {
    if (row.find("symbol")->as_string() == "pdfkit_redact_images") {
      found = true;
      CHECK(row.find("implemented")->as_bool());
    }
  }
  CHECK(found);
}

TEST_CASE("each refusal names what failed, what was expected and the fix") {
  const pdfkit::test::ImageFile file = pdfkit::test::image_file();
  Session session(file.bytes);
  std::string answer;
  CHECK(session.call(treatment("img:0000000000000000", R"("treatment": "box")"), answer) ==
        PDFKIT_E_INVALID_ARGUMENT);
  check_refusal(answer, "invalid_argument", "img:0000000000000000");
  const std::string shared = file.identity.at("shared");
  std::string twice = R"({"treatments": [{"identity": ")";
  twice += shared;
  twice += R"(", "treatment": "box"}, {"identity": ")";
  twice += shared;
  twice += R"(", "treatment": "blur"}]})";
  CHECK(session.call(twice, answer) == PDFKIT_E_INVALID_ARGUMENT);
  check_refusal(answer, "invalid_argument", "treatment 0");
  CHECK(session.call(treatment(shared, R"("treatment": "box", "caption": "Secret")"), answer) ==
        PDFKIT_E_INVALID_ARGUMENT);
  check_refusal(answer, "invalid_argument", "Secret");
  CHECK(session.call(treatment(shared, R"("treatment": "region")"), answer) ==
        PDFKIT_E_INVALID_ARGUMENT);
  check_refusal(answer, "invalid_argument", "treatment 0");
  CHECK(session.call(treatment(shared, R"("treatment": "box", "space": "page", "page": 1, )"
                                       R"("rectangle": [400, 0, 500, 50])"),
                     answer) == PDFKIT_E_INVALID_ARGUMENT);
  check_refusal(answer, "invalid_argument", "treatment 0");
  CHECK(session.call(treatment(file.identity.at("kept"), R"("treatment": "box", "space": "page", )"
                                                         R"("page": 2, "rectangle": [0, 0, 9, 9])"),
                     answer) == PDFKIT_E_INVALID_ARGUMENT);
  check_refusal(answer, "invalid_argument", "page 2");
  CHECK(session.call(treatment(shared, R"("treatment": "fade")"), answer) ==
        PDFKIT_E_INVALID_ARGUMENT);
  check_refusal(answer, "invalid_argument", "fade");
  CHECK(session.call("{\"treatments\": [", answer) == PDFKIT_E_INVALID_ARGUMENT);
  // Every call above was refused: the plan is empty, and a later treatment
  // of the same identity is its first.
  CHECK(session.call(treatment(shared, R"("treatment": "blur")"), answer) == PDFKIT_OK);
  CHECK(session.call(treatment(shared, R"("treatment": "remove")"), answer) ==
        PDFKIT_E_INVALID_ARGUMENT);
  check_refusal(answer, "invalid_argument", "treatment 0 of call");
}

TEST_CASE("a box of an image that does not decode is refused and remove is accepted") {
  pdfkit::test::PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t broken = builder.add_stream(
      "/Type /XObject /Subtype /Image /Width 8 /Height 8 /ColorSpace /DeviceRGB "
      "/BitsPerComponent 8 /Filter /DCTDecode",
      "this is not a JPEG");
  builder.add_page("q 10 0 0 10 0 0 cm /Im0 Do Q",
                   "/Resources << /XObject << /Im0 " + std::to_string(broken) + " 0 R >> >>");
  const std::string file = builder.build();
  const JsonValue listed = pdfkit::test::list_images(file);
  const std::string identity =
      listed.find("images")->as_array().at(0).find("identity")->as_string();
  Session session(file);
  std::string answer;
  CHECK(session.call(treatment(identity, R"("treatment": "box")"), answer) == PDFKIT_E_REFUSED);
  check_refusal(answer, "undecodable_image", identity);
  CHECK(answer.find("remove") != std::string::npos);
  CHECK(session.call(treatment(identity, R"("treatment": "remove")"), answer) == PDFKIT_OK);
  const std::string output = session.write();
  CHECK(output.find("this is not a JPEG") == std::string::npos);
}
