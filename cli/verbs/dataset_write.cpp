// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The channels, write and select members of a dataset outcome. The page
// subset is drawn by SplitMix64 (Steele, Lea and Flood, "Fast splittable
// pseudorandom number generators", 2014, recalled; the constants are those of
// its public-domain reference code), seeded from the SHA-256 of "7:<id>", so
// the subset depends on the id alone.

#include "verbs/dataset_write.hpp"

#include "json_text.hpp"
#include "output.hpp"
#include "sha256.hpp"
#include "support.hpp"

#include <charconv>
#include <optional>
#include <utility>

namespace pdfkit::cli::verbs {

namespace {

struct Call {
  pdfkit_status status = PDFKIT_E_INTERNAL;
  std::string text;
};

Call take(pdfkit_status status, pdfkit_buffer& out) {
  Call call{status, std::string(reinterpret_cast<const char*>(out.data), out.size)};
  pdfkit_buffer_free(&out);
  return call;
}

std::string status_name(pdfkit_status status) {
  switch (status) {
    case PDFKIT_OK:
      return "PDFKIT_OK";
    case PDFKIT_E_INVALID_ARGUMENT:
      return "PDFKIT_E_INVALID_ARGUMENT";
    case PDFKIT_E_REFUSED:
      return "PDFKIT_E_REFUSED";
    case PDFKIT_E_SELF_CHECK:
      return "PDFKIT_E_SELF_CHECK";
    case PDFKIT_E_NOT_IMPLEMENTED:
      return "PDFKIT_E_NOT_IMPLEMENTED";
    case PDFKIT_E_INTERNAL:
      return "PDFKIT_E_INTERNAL";
  }
  return "PDFKIT_E_INTERNAL";
}

// "status", "code" (and "message") of a call that did not succeed, as the
// first members of an object.
std::string failure(const Call& call) {
  std::size_t offset = 0;
  const std::optional<JsonText> error = JsonText::parse(call.text, offset);
  const JsonText::Node* code = error ? error->member(error->root(), "code") : nullptr;
  if (call.status == PDFKIT_E_REFUSED) {
    return "\"status\": \"refused\", \"code\": " +
           json_quote(code != nullptr ? code->text : std::string("refused"));
  }
  if (call.status == PDFKIT_E_NOT_IMPLEMENTED) {
    return R"("status": "not_implemented", "code": "PDFKIT_E_NOT_IMPLEMENTED")";
  }
  const JsonText::Node* failed = error ? error->member(error->root(), "failed") : nullptr;
  return "\"status\": \"error\", \"code\": " + json_quote(status_name(call.status)) +
         ", \"message\": " + json_quote(failed != nullptr ? failed->text : call.text);
}

std::string strings(const std::vector<std::string>& values) {
  std::string out = "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    out += (i == 0 ? "" : ", ") + json_quote(values[i]);
  }
  return out + "]";
}

std::string numbers(const std::vector<std::uint64_t>& values) {
  std::string out = "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    out += (i == 0 ? "" : ", ") + std::to_string(values[i]);
  }
  return out + "]";
}

std::string raw_or_null(const JsonText* document, const JsonText::Node* object,
                        std::string_view key) {
  const JsonText::Node* node =
      document != nullptr && object != nullptr ? document->member(*object, key) : nullptr;
  return node != nullptr ? std::string(node->raw) : std::string("null");
}

// The outcome of opening an output again.
std::string reopen(const std::string& pdf) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_document* document = nullptr;
  const Call opened = take(pdfkit_open(reinterpret_cast<const std::uint8_t*>(pdf.data()),
                                       pdf.size(), nullptr, &document, &out),
                           out);
  pdfkit_close(document);
  if (opened.status != PDFKIT_OK) {
    return "{" + failure(opened) + ", \"pages\": null, \"repairs\": []}";
  }
  std::size_t offset = 0;
  const std::optional<JsonText> report = JsonText::parse(opened.text, offset);
  std::vector<std::string> repairs;
  if (report) {
    if (const JsonText::Node* list = report->member(report->root(), "repairs")) {
      for (const std::size_t index : list->items) {
        repairs.push_back(report->node(index).text);
      }
    }
  }
  return R"({"status": "ok", "code": null, "pages": )" +
         raw_or_null(report ? &*report : nullptr, report ? &report->root() : nullptr,
                     "page_count") +
         ", \"repairs\": " + strings(repairs) + "}";
}

struct Written {
  Call call;
  std::string pdf;
};

// Writes document with pages_json selected (empty for the identity write).
Written write(pdfkit_document* document, const std::string& pages_json) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_edit* edit = nullptr;
  Written written;
  written.call = take(pdfkit_edit_new(document, &edit, &out), out);
  if (written.call.status == PDFKIT_OK && !pages_json.empty()) {
    written.call = take(pdfkit_select_pages(edit, pages_json.c_str(), &out), out);
  }
  if (written.call.status == PDFKIT_OK) {
    pdfkit_buffer pdf{nullptr, 0, nullptr};
    written.call = take(pdfkit_write(edit, nullptr, &pdf, &out), out);
    written.pdf.assign(reinterpret_cast<const char*>(pdf.data), pdf.size);
    pdfkit_buffer_free(&pdf);
  }
  pdfkit_edit_free(edit);
  return written;
}

// The output's members: its relative path, hash, size, and the file written.
std::optional<std::string> store(const Written& written, const std::string& out_dir,
                                 const std::string& name) {
  std::string path = out_dir;
  if (!path.empty() && path.back() != '/' && path.back() != '\\') {
    path += '/';
  }
  const std::string relative = "outputs/" + name;
  const auto* data = reinterpret_cast<const std::uint8_t*>(written.pdf.data());
  if (!write_output_file("dataset", path + relative, data, written.pdf.size())) {
    return std::nullopt;
  }
  return "\"output\": " + json_quote(relative) + ", \"sha256\": " +
         json_quote(Sha256::hex(data, written.pdf.size())) +
         ", \"bytes\": " + std::to_string(written.pdf.size());
}

std::uint64_t splitmix(std::uint64_t& state) {
  state += 0x9E3779B97F4A7C15ULL;
  std::uint64_t z = state;
  z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31U);
}

}  // namespace

std::vector<std::uint64_t> dataset_selection(std::string_view id, std::uint64_t pages) {
  const std::string seed_text = "7:" + std::string(id);
  const std::string digest =
      Sha256::hex(reinterpret_cast<const std::uint8_t*>(seed_text.data()), seed_text.size());
  std::uint64_t state = 0;
  (void)std::from_chars(digest.data(), digest.data() + 16, state, 16);
  std::vector<std::uint64_t> kept;
  for (std::uint64_t page = 1; page <= pages; ++page) {
    if ((splitmix(state) >> 63U) != 0) {
      kept.push_back(page);
    }
  }
  if (kept.empty()) {
    kept.push_back(1);
  } else if (kept.size() == pages) {
    kept.pop_back();
  }
  for (std::size_t i = kept.size() - 1; i > 0; --i) {
    const std::uint64_t j = splitmix(state) % (i + 1);
    std::swap(kept[i], kept[j]);
  }
  return kept;
}

DatasetWrite dataset_write_skipped() {
  const std::string skipped = R"({"status": "skipped", "code": null})";
  return {skipped, skipped, skipped};
}

DatasetWrite dataset_write(pdfkit_document* document, std::string_view id, std::uint64_t pages,
                           const std::string& out_dir) {
  DatasetWrite members;
  pdfkit_buffer out{nullptr, 0, nullptr};
  const Call channels = take(pdfkit_channels(document, nullptr, &out), out);
  if (channels.status == PDFKIT_OK) {
    std::size_t offset = 0;
    const std::optional<JsonText> report = JsonText::parse(channels.text, offset);
    members.channels = R"({"status": "ok", "code": null, "present": )" +
                       raw_or_null(report ? &*report : nullptr,
                                   report ? &report->root() : nullptr, "present") +
                       "}";
  } else {
    members.channels = "{" + failure(channels) + "}";
  }

  const Written identity = write(document, "");
  if (identity.call.status != PDFKIT_OK) {
    members.write = "{" + failure(identity.call) + "}";
  } else {
    std::size_t offset = 0;
    const std::optional<JsonText> report = JsonText::parse(identity.call.text, offset);
    std::vector<std::string> stripped;
    if (report) {
      if (const JsonText::Node* list = report->member(report->root(), "channels")) {
        for (const std::size_t index : list->items) {
          const JsonText::Node& row = report->node(index);
          const JsonText::Node* count = report->member(row, "stripped");
          const JsonText::Node* row_id = report->member(row, "id");
          if (count != nullptr && row_id != nullptr && count->raw != "0") {
            stripped.push_back(row_id->text);
          }
        }
      }
    }
    const std::optional<std::string> stored =
        store(identity, out_dir, std::string(id) + ".pdf");
    if (!stored) {
      members.write = R"({"status": "error", "code": "output_failed", "message": "the output could not be written"})";
    } else {
      const JsonText* document_report = report ? &*report : nullptr;
      const JsonText::Node* root = report ? &report->root() : nullptr;
      members.write = R"({"status": "ok", "code": null, )" + *stored +
                      ", \"pages\": " + raw_or_null(document_report, root, "page_count") +
                      ", \"stripped\": " + strings(stripped) + ", \"missing_references\": " +
                      raw_or_null(document_report, root, "missing_references") +
                      ", \"reopen\": " + reopen(identity.pdf) + "}";
    }
  }

  if (pages < 2) {
    members.select = "null";
    return members;
  }
  const std::vector<std::uint64_t> kept = dataset_selection(id, pages);
  const Written selected = write(document, "{\"pages\": " + numbers(kept) + "}");
  if (selected.call.status != PDFKIT_OK) {
    members.select = "{" + failure(selected.call) + ", \"pages\": " + numbers(kept) + "}";
    return members;
  }
  const std::optional<std::string> stored =
      store(selected, out_dir, std::string(id) + ".select.pdf");
  if (!stored) {
    members.select = R"({"status": "error", "code": "output_failed", "message": "the output could not be written"})";
    return members;
  }
  members.select = R"({"status": "ok", "code": null, "pages": )" + numbers(kept) + ", " +
                   *stored + ", \"reopen\": " + reopen(selected.pdf) + "}";
  return members;
}

}  // namespace pdfkit::cli::verbs
