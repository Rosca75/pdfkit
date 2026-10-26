// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 6, Redact images: pdfkit_redact_images adds image
// treatments to an edit plan per image identity (CLAUDE.md section 2, the
// pdfkit_redact_images section of docs/ABI.md): a box over the whole image or
// a rectangle of it, with an optional caption; a blur; a removal; and region
// boxes, each a box over a rectangle the caller found in the image's pixels,
// in pixel or page space. It answers per treatment the pixel rectangles it
// paints and the draws it reaches with their device rectangles. Every
// treatment is checked before any is recorded, so a refused call leaves the
// plan as it was; the pixels change when the plan is written.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "images/image_listing.hpp"
#include "platform/error.hpp"
#include "platform/export.hpp"
#include "platform/json_value.hpp"
#include "platform/json_writer.hpp"
#include "redact/image_treatment_plan.hpp"

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;
using pdfkit::platform::JsonValue;
using pdfkit::redact::ImageTreatmentPlan;

constexpr std::string_view shape =
    R"({"treatments": [...]}, each {"identity": "<from pdfkit_images>", "treatment": "box" | "blur" | "remove" | "region", "rectangle": [4 numbers], "space": "image" | "page", "page": <n>, "caption": "<text>", "colour": [r, g, b]})";

[[noreturn]] void refuse(const std::string& failed, const std::string& fix) {
  throw Error(ErrorCode::invalid_argument, "pdfkit_redact_images treatments_json " + failed,
              std::string(shape), fix);
}

std::string where(std::size_t index) {
  return "treatment " + std::to_string(index);
}

const std::string& text_member(const JsonValue& value, std::size_t index, const std::string& name) {
  if (value.kind() != JsonValue::Kind::string) {
    refuse(where(index) + " gives \"" + name + "\" as " +
               std::string(JsonValue::kind_name(value.kind())),
           "write \"" + name + "\" as a string");
  }
  return value.as_string();
}

std::vector<double> numbers(const JsonValue& value, std::size_t index, const std::string& name,
                            std::size_t count) {
  const bool array = value.kind() == JsonValue::Kind::array;
  if (!array || value.as_array().size() != count) {
    refuse(where(index) + " gives \"" + name + "\" as something other than " +
               std::to_string(count) + " numbers",
           "write \"" + name + "\" as an array of " + std::to_string(count) + " numbers");
  }
  std::vector<double> out;
  for (const JsonValue& item : value.as_array()) {
    if (item.kind() != JsonValue::Kind::number || !std::isfinite(item.as_number())) {
      refuse(where(index) + " gives \"" + name + "\" a value that is not a finite number",
             "write \"" + name + "\" as an array of " + std::to_string(count) + " numbers");
    }
    out.push_back(item.as_number());
  }
  return out;
}

ImageTreatmentPlan::Request read_request(const JsonValue& item, std::size_t index) {
  if (item.kind() != JsonValue::Kind::object) {
    refuse(where(index) + " is " + std::string(JsonValue::kind_name(item.kind())),
           "write each treatment as an object");
  }
  ImageTreatmentPlan::Request request;
  bool identity = false;
  bool kind = false;
  std::string space = "image";
  bool page = false;
  for (const auto& [name, value] : item.as_object()) {
    if (name == "identity") {
      identity = true;
      request.identity = text_member(value, index, name);
    } else if (name == "treatment") {
      kind = true;
      const std::string& text = text_member(value, index, name);
      if (text == "box") {
        request.kind = ImageTreatmentPlan::Kind::box;
      } else if (text == "blur") {
        request.kind = ImageTreatmentPlan::Kind::blur;
      } else if (text == "remove") {
        request.kind = ImageTreatmentPlan::Kind::remove;
      } else if (text == "region") {
        request.kind = ImageTreatmentPlan::Kind::region;
      } else {
        refuse(where(index) + " names the unknown treatment \"" + text + "\"",
               "use box, blur, remove or region");
      }
    } else if (name == "rectangle") {
      const std::vector<double> values = numbers(value, index, name, 4);
      request.rectangle = std::array<double, 4>{values[0], values[1], values[2], values[3]};
    } else if (name == "space") {
      space = text_member(value, index, name);
      if (space != "image" && space != "page") {
        refuse(where(index) + " names the unknown space \"" + space + "\"",
               R"(write "space": "image" (pixels) or "page" (default user space))");
      }
    } else if (name == "page") {
      page = true;
      const std::optional<std::int64_t> number =
          value.kind() == JsonValue::Kind::number ? value.as_int64() : std::nullopt;
      if (!number || *number < 1 || std::cmp_greater(*number, 0xFFFFFFFFU)) {
        refuse(where(index) + " gives \"page\" as something other than a page number",
               "write \"page\" as a whole number from 1");
      }
      request.page = static_cast<std::uint32_t>(*number);
    } else if (name == "caption") {
      request.caption = text_member(value, index, name);
    } else if (name == "colour") {
      const std::vector<double> values = numbers(value, index, name, 3);
      for (const double v : values) {
        if (v < 0.0 || v > 1.0) {
          refuse(where(index) + " gives \"colour\" a value outside 0 to 1",
                 "write the colour as red, green and blue from 0 to 1, for example [0, 0, 0]");
        }
      }
      request.colour = ImageTreatmentPlan::Colour{values[0], values[1], values[2]};
    } else {
      refuse(where(index) + " names the unknown key \"" + name + "\"",
             "use identity, treatment, rectangle, space, page, caption and colour only");
    }
  }
  if (!identity || !kind) {
    refuse(where(index) + (identity ? R"( has no "treatment")" : R"( has no "identity")"),
           "give each treatment an identity from pdfkit_images and a treatment");
  }
  request.page_space = space == "page";
  if (request.page_space && (!page || !request.rectangle)) {
    refuse(where(index) + R"( gives the page space without a "rectangle" and a "page")",
           "give the page rectangle [left, bottom, right, top] and the page it is on");
  }
  if (!request.page_space && page) {
    refuse(where(index) + R"( gives a "page" with a rectangle in pixels)",
           R"(write "space": "page" with a page rectangle, or drop the page)");
  }
  if (request.kind == ImageTreatmentPlan::Kind::region && !request.rectangle) {
    refuse(where(index) + " is a region without a \"rectangle\"",
           "give the region's rectangle, or use a box over the whole image");
  }
  return request;
}

void write_rounded(pdfkit::platform::JsonWriter& writer, double value) {
  writer.number(std::round(value * 100.0) / 100.0);
}

}  // namespace

extern "C" PDFKIT_API pdfkit_status pdfkit_redact_images(pdfkit_edit* edit,
                                                         const char* treatments_json,
                                                         pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_redact_images", "out");
    pdfkit_edit& handle = pdfkit::api::require_edit(edit, "pdfkit_redact_images");
    const JsonValue input = pdfkit::api::parse_json_object(treatments_json, "pdfkit_redact_images",
                                                           "treatments_json", true);
    for (const auto& [name, value] : input.as_object()) {
      if (name != "treatments") {
        refuse("names the unknown key \"" + name + "\"", "give only \"treatments\"");
      }
    }
    const JsonValue* list = input.find("treatments");
    if (list == nullptr || list->kind() != JsonValue::Kind::array) {
      refuse("has no \"treatments\" array", "list the treatments as {\"treatments\": [...]}");
    }
    std::vector<ImageTreatmentPlan::Request> requests;
    requests.reserve(list->as_array().size());
    for (std::size_t i = 0; i < list->as_array().size(); ++i) {
      requests.push_back(read_request(list->as_array()[i], i));
    }
    pdfkit_document& document = *handle.document;
    if (!document.listing) {
      document.listing = std::make_unique<pdfkit::images::ImageListing>(*document.document);
    }
    const std::vector<ImageTreatmentPlan::Answer> answers =
        handle.plan->images().add(*document.document, *document.listing, requests);

    pdfkit::platform::JsonWriter writer;
    writer.begin_object();
    writer.key("treatments").begin_array();
    for (std::size_t i = 0; i < answers.size(); ++i) {
      const ImageTreatmentPlan::Answer& answer = answers[i];
      writer.begin_object();
      writer.key("treatment").unsigned_integer(i);
      writer.key("identity").string(answer.identity);
      writer.key("kind").string(ImageTreatmentPlan::name(answer.kind));
      writer.key("width").unsigned_integer(answer.width);
      writer.key("height").unsigned_integer(answer.height);
      writer.key("pixels").begin_array();
      for (const ImageTreatmentPlan::PixelRect& rect : answer.pixels) {
        writer.begin_array()
            .unsigned_integer(rect.left)
            .unsigned_integer(rect.top)
            .unsigned_integer(rect.right)
            .unsigned_integer(rect.bottom)
            .end_array();
      }
      writer.end_array();
      writer.key("block");
      if (answer.block > 0) {
        writer.unsigned_integer(answer.block);
      } else {
        writer.null();
      }
      writer.key("encoded").begin_object();
      writer.key("sha256").string(answer.encoded_sha256);
      writer.key("bytes").unsigned_integer(answer.encoded_bytes);
      writer.end_object();
      writer.key("occurrences").begin_array();
      for (const ImageTreatmentPlan::Answer::Draw& draw : answer.occurrences) {
        writer.begin_object();
        writer.key("page").unsigned_integer(draw.page);
        writer.key("ordinal").unsigned_integer(draw.ordinal);
        writer.key("rectangles").begin_array();
        for (const pdfkit::text::Rect& box : draw.rectangles) {
          writer.begin_array();
          for (const double value : {box.left, box.bottom, box.right, box.top}) {
            write_rounded(writer, value);
          }
          writer.end_array();
        }
        writer.end_array();
        writer.end_object();
      }
      writer.end_array();
      writer.end_object();
    }
    writer.end_array();
    writer.end_object();
    pdfkit::api::write_buffer(out, writer.take());
    return PDFKIT_OK;
  });
}
