// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The redact_images member of a dataset outcome. SplitMix64 seeded with the
// first eight bytes of the SHA-256 of "9:<id>" draws, in order: the start of
// the treatment cycle (one number modulo 3), the image the region box goes
// on (modulo the kept images), the draw it is placed over (modulo its
// draws), and four fractions of that draw's box (or of the image's pixels
// when it has no draw) that make the region's rectangle. The region is its
// own call after the treatments, so a refused region (a draw of no area) is
// recorded without losing the rest of the job.

#include "verbs/dataset_redact_images.hpp"

#include "json_text.hpp"
#include "output.hpp"
#include "sha256.hpp"
#include "support.hpp"
#include "verbs/dataset_write.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace pdfkit::cli::verbs {

namespace {

constexpr std::string_view caption = "REDACTED";
// A bitonal image under this side is boxed rather than blurred, the library's
// refusal threshold (docs/DESIGN.md section 14).
constexpr double smallest_bitonal_blur = 128.0;

struct Call {
  pdfkit_status status = PDFKIT_E_INTERNAL;
  std::string text;
};

Call take(pdfkit_status status, pdfkit_buffer& out) {
  Call call{status, std::string(reinterpret_cast<const char*>(out.data), out.size)};
  pdfkit_buffer_free(&out);
  return call;
}

struct Image {
  std::string identity;
  bool decodable = false;
  bool bitonal = false;
  double width = 0;
  double height = 0;
  // Per draw: page and the box's four numbers as written.
  std::vector<std::pair<std::string, std::vector<double>>> draws;
  std::string kind;
  bool captioned = false;
  // The encoding as pdfkit_images writes it, a JSON string.
  std::string encoding = "null";
};

double number(const JsonText::Node* node) {
  if (node == nullptr || node->kind != JsonText::Kind::number) {
    return 0.0;
  }
  double value = 0.0;
  (void)std::from_chars(node->raw.data(), node->raw.data() + node->raw.size(), value);
  return value;
}

std::string decimal(double value) {
  const double rounded = std::round(value * 100.0) / 100.0;
  std::array<char, 64> buffer{};
  const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), rounded);
  return error == std::errc() ? std::string(buffer.data(), end) : std::string("0");
}

std::vector<Image> images_of(const std::string& listed) {
  std::vector<Image> out;
  std::size_t offset = 0;
  const std::optional<JsonText> json = JsonText::parse(listed, offset);
  const JsonText::Node* list = json ? json->member(json->root(), "images") : nullptr;
  if (list == nullptr) {
    return out;
  }
  for (const std::size_t index : list->items) {
    const JsonText::Node& entry = json->node(index);
    Image image;
    if (const JsonText::Node* identity = json->member(entry, "identity")) {
      image.identity = identity->text;
    }
    const JsonText::Node* decodable = json->member(entry, "decodable");
    image.decodable = decodable != nullptr && decodable->text == "true";
    const JsonText::Node* mask = json->member(entry, "image_mask");
    const JsonText::Node* bits = json->member(entry, "bits_per_component");
    image.bitonal =
        (mask != nullptr && mask->text == "true") || (bits != nullptr && bits->raw == "1");
    if (const JsonText::Node* encoding = json->member(entry, "encoding")) {
      image.encoding = std::string(encoding->raw);
    }
    image.width = number(json->member(entry, "width"));
    image.height = number(json->member(entry, "height"));
    if (const JsonText::Node* draws = json->member(entry, "occurrences")) {
      for (const std::size_t at : draws->items) {
        const JsonText::Node& draw = json->node(at);
        const JsonText::Node* page = json->member(draw, "page");
        const JsonText::Node* box = json->member(draw, "box");
        std::vector<double> values;
        for (const std::size_t v : box != nullptr ? box->items : std::vector<std::size_t>{}) {
          values.push_back(number(&json->node(v)));
        }
        if (page != nullptr && values.size() == 4) {
          image.draws.emplace_back(std::string(page->raw), std::move(values));
        }
      }
    }
    out.push_back(std::move(image));
  }
  return out;
}

std::string quoted_list(const std::vector<std::string>& values) {
  std::string out = "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    out += (i == 0 ? "" : ", ") + json_quote(values[i]);
  }
  return out + "]";
}

// The identities pdfkit_images lists for pdf.
std::set<std::string> listed_identities(const std::string& pdf) {
  std::set<std::string> out;
  pdfkit_buffer buffer{nullptr, 0, nullptr};
  pdfkit_document* document = nullptr;
  const Call opened = take(pdfkit_open(reinterpret_cast<const std::uint8_t*>(pdf.data()),
                                       pdf.size(), nullptr, &document, &buffer),
                           buffer);
  if (opened.status != PDFKIT_OK) {
    return out;
  }
  const Call listed = take(pdfkit_images(document, nullptr, &buffer), buffer);
  pdfkit_close(document);
  for (const Image& image : images_of(listed.text)) {
    out.insert(image.identity);
  }
  return out;
}

}  // namespace

std::string dataset_redact_images(pdfkit_document* document, std::string_view id,
                                  const std::string& out_dir) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  const Call listed = take(pdfkit_images(document, nullptr, &out), out);
  if (listed.status != PDFKIT_OK) {
    return "{" + dataset_failure(listed.status, listed.text) + "}";
  }
  std::vector<Image> images = images_of(listed.text);
  if (images.empty()) {
    return R"({"status": "ok", "code": null, "treatments": [], "region": null, "output": null})";
  }
  const std::string seed_text = "9:" + std::string(id);
  const std::string seed_digest =
      Sha256::hex(reinterpret_cast<const std::uint8_t*>(seed_text.data()), seed_text.size());
  std::uint64_t state = 0;
  (void)std::from_chars(seed_digest.data(), seed_digest.data() + 16, state, 16);
  const std::uint64_t start = dataset_splitmix(state) % 3;
  std::string treatments = R"({"treatments": [)";
  std::size_t boxes = 0;
  for (std::size_t i = 0; i < images.size(); ++i) {
    Image& image = images[i];
    static constexpr std::array<std::string_view, 3> cycle{"box", "blur", "remove"};
    image.kind = std::string(cycle[(start + i) % 3]);
    if (!image.decodable) {
      image.kind = "remove";
    } else if (image.kind == "blur" && image.bitonal &&
               std::min(image.width, image.height) < smallest_bitonal_blur) {
      image.kind = "box";
    }
    std::string entry = R"({"identity": )" + json_quote(image.identity) + R"(, "treatment": )" +
                        json_quote(image.kind);
    if (image.kind == "box") {
      image.captioned = boxes % 2 == 1;
      if (image.captioned) {
        entry += R"(, "caption": )" + json_quote(caption);
      }
      ++boxes;
    }
    treatments += (i == 0 ? "" : ", ") + entry + "}";
  }
  treatments += "]}";
  // The region: one kept image, over one of its draws or its pixels.
  std::vector<std::size_t> kept;
  for (std::size_t i = 0; i < images.size(); ++i) {
    if (images[i].kind != "remove") {
      kept.push_back(i);
    }
  }
  std::string region;
  std::string region_identity;
  if (!kept.empty()) {
    const Image& image = images[kept[dataset_splitmix(state) % kept.size()]];
    region_identity = image.identity;
    const auto fraction = [&state](std::uint64_t span) {
      return static_cast<double>(dataset_splitmix(state) % span) / 100.0;
    };
    const double x0 = fraction(50);
    const double x1 = std::min(1.0, x0 + 0.1 + fraction(41));
    const double y0 = fraction(50);
    const double y1 = std::min(1.0, y0 + 0.1 + fraction(41));
    if (!image.draws.empty()) {
      const auto& [page, box] = image.draws[dataset_splitmix(state) % image.draws.size()];
      const double w = box[2] - box[0];
      const double h = box[3] - box[1];
      region = R"({"identity": )" + json_quote(image.identity) +
               R"(, "treatment": "region", "space": "page", "page": )" + page +
               R"(, "rectangle": [)" + decimal(box[0] + (x0 * w)) + ", " +
               decimal(box[1] + (y0 * h)) + ", " + decimal(box[0] + (x1 * w)) + ", " +
               decimal(box[1] + (y1 * h)) + "]}";
    } else {
      region = R"({"identity": )" + json_quote(image.identity) +
               R"(, "treatment": "region", "rectangle": [)" + decimal(x0 * image.width) + ", " +
               decimal(y0 * image.height) + ", " + decimal(x1 * image.width) + ", " +
               decimal(y1 * image.height) + "]}";
    }
  }

  pdfkit_edit* edit = nullptr;
  Call call = take(pdfkit_edit_new(document, &edit, &out), out);
  std::string answer;
  if (call.status == PDFKIT_OK) {
    call = take(pdfkit_redact_images(edit, treatments.c_str(), &out), out);
    answer = call.text;
  }
  std::string region_answer = "null";
  std::string region_status = "null";
  if (call.status == PDFKIT_OK && !region.empty()) {
    const std::string asked = R"({"treatments": [)" + region + "]}";
    const Call regioned = take(pdfkit_redact_images(edit, asked.c_str(), &out), out);
    region_status = regioned.status == PDFKIT_OK ? R"("ok")" : R"("refused")";
    region_answer = regioned.text;
  }
  std::string pdf;
  if (call.status == PDFKIT_OK) {
    pdfkit_buffer bytes{nullptr, 0, nullptr};
    call = take(pdfkit_write(edit, nullptr, &bytes, &out), out);
    pdf.assign(reinterpret_cast<const char*>(bytes.data), bytes.size);
    pdfkit_buffer_free(&bytes);
  }
  pdfkit_edit_free(edit);
  if (call.status != PDFKIT_OK) {
    return "{" + dataset_failure(call.status, call.text) + ", \"job\": " + treatments + "}";
  }

  // Per treatment, what the answer says it paints.
  std::string described = "[";
  std::size_t offset = 0;
  if (const std::optional<JsonText> found = JsonText::parse(answer, offset)) {
    if (const JsonText::Node* items = found->member(found->root(), "treatments")) {
      for (std::size_t i = 0; i < items->items.size() && i < images.size(); ++i) {
        const JsonText::Node& item = found->node(items->items[i]);
        const auto raw = [&](std::string_view key) {
          const JsonText::Node* node = found->member(item, key);
          return node != nullptr ? std::string(node->raw) : std::string("null");
        };
        std::string draws = "[";
        for (std::size_t d = 0; d < images[i].draws.size(); ++d) {
          const auto& [page, box] = images[i].draws[d];
          draws += (d == 0 ? "[" : ", [") + page;
          for (const double value : box) {
            draws += ", " + decimal(value);
          }
          draws += "]";
        }
        draws += "]";
        described += (i == 0 ? "" : ", ") + std::string(R"({"identity": )") +
                     json_quote(images[i].identity) + R"(, "kind": )" + json_quote(images[i].kind) +
                     R"(, "caption": )" + (images[i].captioned ? "true" : "false") +
                     R"(, "encoding": )" + images[i].encoding + R"(, "width": )" + raw("width") +
                     R"(, "height": )" + raw("height") + R"(, "pixels": )" + raw("pixels") +
                     R"(, "block": )" + raw("block") + R"(, "encoded": )" + raw("encoded") +
                     R"(, "draws": )" + draws + "}";
      }
    }
  }
  described += "]";
  std::string region_member = "null";
  if (!region.empty()) {
    std::string pixels = "null";
    if (const std::optional<JsonText> found = JsonText::parse(region_answer, offset)) {
      if (const JsonText::Node* items = found->member(found->root(), "treatments")) {
        if (!items->items.empty()) {
          if (const JsonText::Node* node = found->member(found->node(items->items[0]), "pixels")) {
            pixels = std::string(node->raw);
          }
        }
      }
    }
    region_member = R"({"identity": )" + json_quote(region_identity) + R"(, "status": )" +
                    region_status + R"(, "pixels": )" + pixels + "}";
  }
  // The rectangles and the image entries of the write report.
  std::string rectangles = "[";
  std::string treated = "[]";
  std::string pages = "null";
  std::set<std::string> numbered;
  if (const std::optional<JsonText> report = JsonText::parse(call.text, offset)) {
    if (const JsonText::Node* count = report->member(report->root(), "page_count")) {
      pages = std::string(count->raw);
    }
    if (const JsonText::Node* entries = report->member(report->root(), "images")) {
      treated = std::string(entries->raw);
    }
    if (const JsonText::Node* changed = report->member(report->root(), "rectangles")) {
      bool first_box = true;
      for (const std::size_t index : changed->items) {
        const JsonText::Node& entry = report->node(index);
        const JsonText::Node* page = report->member(entry, "page");
        const std::string page_number = page != nullptr ? std::string(page->raw) : "0";
        numbered.insert(page_number);
        if (const JsonText::Node* list = report->member(entry, "boxes")) {
          for (const std::size_t box : list->items) {
            const std::string_view raw = report->node(box).raw;
            rectangles += (first_box ? "" : ", ") + std::string("[") + page_number + ", " +
                          std::string(raw.substr(1));
            first_box = false;
          }
        }
      }
    }
  }
  rectangles += ']';
  // Each rectangle page's crop box and rotation, as the text member records
  // them (the job selects no page, so an output page is its source page).
  std::string geometry = "[";
  {
    pdfkit_buffer inspected{nullptr, 0, nullptr};
    const Call inspect = take(pdfkit_inspect(document, nullptr, &inspected), inspected);
    const std::optional<JsonText> shape = JsonText::parse(inspect.text, offset);
    const JsonText* pages_of = shape && inspect.status == PDFKIT_OK ? &*shape : nullptr;
    const JsonText::Node* list =
        pages_of != nullptr ? pages_of->member(pages_of->root(), "pages") : nullptr;
    bool first = true;
    for (const std::size_t index : list != nullptr ? list->items : std::vector<std::size_t>{}) {
      const JsonText::Node& entry = pages_of->node(index);
      const JsonText::Node* page = pages_of->member(entry, "page");
      const JsonText::Node* crop = pages_of->member(entry, "crop_box");
      const JsonText::Node* rotate = pages_of->member(entry, "rotate");
      if (page == nullptr || crop == nullptr || rotate == nullptr ||
          !numbered.contains(std::string(page->raw))) {
        continue;
      }
      geometry += (first ? "[" : ", [") + std::string(page->raw) + ", " +
                  std::string(crop->raw.substr(1, crop->raw.size() - 2)) + ", " +
                  std::string(rotate->raw) + "]";
      first = false;
    }
  }
  geometry += ']';
  // The identities of the input still listed in the output: none may be,
  // unless a treatment wrote exactly that image's bytes (a blur of an image
  // whose blocks were already constant writes the image again, and the same
  // bytes may be another image of the input), which is listed as unchanged.
  std::set<std::string> produced;
  if (const std::optional<JsonText> report = JsonText::parse(call.text, offset)) {
    if (const JsonText::Node* entries = report->member(report->root(), "images")) {
      for (const std::size_t index : entries->items) {
        if (const JsonText::Node* digests = report->member(report->node(index), "sha256")) {
          for (const std::size_t digest : digests->items) {
            produced.insert(report->node(digest).text);
          }
        }
      }
    }
  }
  std::map<std::string, std::string> encoded_of;
  if (const std::optional<JsonText> found = JsonText::parse(answer, offset)) {
    if (const JsonText::Node* items = found->member(found->root(), "treatments")) {
      for (const std::size_t index : items->items) {
        const JsonText::Node& item = found->node(index);
        const JsonText::Node* identity = found->member(item, "identity");
        const JsonText::Node* encoded = found->member(item, "encoded");
        const JsonText::Node* digest =
            encoded != nullptr ? found->member(*encoded, "sha256") : nullptr;
        if (identity != nullptr && digest != nullptr) {
          encoded_of[identity->text] = digest->text;
        }
      }
    }
  }
  std::vector<std::string> survivors;
  std::vector<std::string> unchanged;
  const std::set<std::string> left = listed_identities(pdf);
  for (const Image& image : images) {
    if (!left.contains(image.identity)) {
      continue;
    }
    const auto digest = encoded_of.find(image.identity);
    if (digest != encoded_of.end() && produced.contains(digest->second)) {
      unchanged.push_back(image.identity);
    } else {
      survivors.push_back(image.identity);
    }
  }

  std::string path = out_dir;
  if (!path.empty() && path.back() != '/' && path.back() != '\\') {
    path += '/';
  }
  const std::string relative = "outputs/" + std::string(id) + ".images.pdf";
  const auto* data = reinterpret_cast<const std::uint8_t*>(pdf.data());
  if (!write_output_file("dataset", path + relative, data, pdf.size())) {
    return R"({"status": "error", "code": "output_failed", "message": "the output could not be written"})";
  }
  return R"({"status": "ok", "code": null, "treatments": )" + described + R"(, "region": )" +
         region_member + R"(, "output": )" + json_quote(relative) + R"(, "sha256": )" +
         json_quote(Sha256::hex(data, pdf.size())) + R"(, "bytes": )" + std::to_string(pdf.size()) +
         R"(, "pages": )" + pages + R"(, "rectangles": )" + rectangles + R"(, "geometry": )" +
         geometry + R"(, "images": )" + treated + R"(, "survivors": )" + quoted_list(survivors) +
         R"(, "unchanged": )" + quoted_list(unchanged) + R"(, "reopen": )" + dataset_reopen(pdf) +
         "}";
}

}  // namespace pdfkit::cli::verbs
