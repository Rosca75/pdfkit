// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The metadata members of a dataset outcome. The rewrite job sets each field
// the file holds to "REDACTED " and the field's name in capitals, and adds
// every original value of those fields as a text search with the default
// folding and the placeholder [REDACTED], so an original the page text also
// holds is redacted there rather than refused; its result is read back
// through pdfkit_metadata, field by field, in both places.

#include "verbs/dataset_metadata.hpp"

#include "json_text.hpp"
#include "output.hpp"
#include "sha256.hpp"
#include "support.hpp"
#include "verbs/dataset_write.hpp"

#include <algorithm>
#include <optional>
#include <set>

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

std::string list(const std::vector<std::string>& values) {
  std::string out = "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    out += (i == 0 ? "" : ", ") + json_quote(values[i]);
  }
  return out + "]";
}

bool blank(std::string_view text) {
  return std::ranges::all_of(
      text, [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; });
}

std::string lower(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return out;
}

std::string written_value(std::string_view field) {
  std::string out = "REDACTED ";
  for (const char c : field) {
    out += (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
  }
  return out;
}

// The rows of a pdfkit_metadata answer: field, Info value, XMP values.
struct Row {
  std::string field;
  std::optional<std::string> info;
  std::vector<std::string> xmp;
  // The items of the XMP arrays.
  std::vector<std::string> items;
  bool disagree = false;
  bool utf16 = false;
};

std::vector<Row> rows_of(const JsonText& answer) {
  std::vector<Row> rows;
  const JsonText::Node* fields = answer.member(answer.root(), "fields");
  for (const std::size_t index : fields != nullptr ? fields->items : std::vector<std::size_t>{}) {
    const JsonText::Node& node = answer.node(index);
    Row row;
    if (const JsonText::Node* name = answer.member(node, "field")) {
      row.field = name->text;
    }
    if (const JsonText::Node* info = answer.member(node, "info");
        info != nullptr && info->kind == JsonText::Kind::string) {
      row.info = info->text;
    }
    if (const JsonText::Node* values = answer.member(node, "xmp_values")) {
      for (const std::size_t at : values->items) {
        if (const JsonText::Node* value = answer.member(answer.node(at), "value")) {
          row.xmp.push_back(value->text);
        }
        if (const JsonText::Node* items = answer.member(answer.node(at), "items")) {
          for (const std::size_t item : items->items) {
            row.items.push_back(answer.node(item).text);
          }
        }
      }
    }
    if (const JsonText::Node* agree = answer.member(node, "agree")) {
      row.disagree = agree->raw == "false";
    }
    if (const JsonText::Node* encoding = answer.member(node, "info_encoding")) {
      row.utf16 = encoding->text == "utf16be" || encoding->text == "utf16le";
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

}  // namespace

DatasetMetadata dataset_metadata(pdfkit_document* document) {
  DatasetMetadata result;
  pdfkit_buffer out{nullptr, 0, nullptr};
  const Call call = take(pdfkit_metadata(document, nullptr, &out), out);
  if (call.status != PDFKIT_OK) {
    result.member = "{" + dataset_failure(call.status, call.text) + "}";
    return result;
  }
  std::size_t offset = 0;
  const std::optional<JsonText> answer = JsonText::parse(call.text, offset);
  if (!answer) {
    result.member = R"({"status": "error", "code": "internal", "message": "unparsable answer"})";
    return result;
  }
  std::vector<std::string> present;
  std::vector<std::string> disagree;
  std::size_t utf16 = 0;
  for (const Row& row : rows_of(*answer)) {
    std::vector<std::string> values;
    const auto add = [&values](const std::string& value) {
      if (!blank(value) && std::ranges::find(values, value) == values.end()) {
        values.push_back(value);
      }
    };
    if (row.info) {
      add(*row.info);
    }
    std::ranges::for_each(row.xmp, add);
    std::ranges::for_each(row.items, add);
    if (row.info || !row.xmp.empty()) {
      present.push_back(row.field);
      result.fields.emplace_back(row.field, std::move(values));
    }
    if (row.disagree) {
      disagree.push_back(row.field);
    }
    utf16 += row.utf16 ? 1 : 0;
  }
  const auto raw = [&answer](std::string_view key, std::string_view fallback) {
    const JsonText::Node* node = answer->member(answer->root(), key);
    return std::string(node != nullptr ? node->raw : fallback);
  };
  result.member = R"({"status": "ok", "code": null, "info": )" + raw("info", "null") +
                  ", \"xmp\": " + raw("xmp", "null") + ", \"agree\": " + raw("agree", "null") +
                  ", \"present\": " + list(present) + ", \"disagree\": " + list(disagree) +
                  ", \"utf16\": " + std::to_string(utf16) +
                  ", \"utf16le\": " + raw("utf16le", "0") +
                  ", \"namespaces\": " + raw("xmp_namespaces", "[]") +
                  ", \"history\": " + raw("xmp_history", "false") + "}";
  return result;
}

std::string dataset_metadata_rewrite(const DatasetMetadata& metadata,
                                     const std::vector<std::string>& strings,
                                     std::vector<std::string>& names) {
  std::string fields;
  for (const auto& [field, values] : metadata.fields) {
    const bool holds = std::ranges::any_of(values, [&strings](const std::string& value) {
      const std::string folded = lower(value);
      return std::ranges::any_of(strings, [&folded](const std::string& string) {
        return !string.empty() && folded.find(lower(string)) != std::string::npos;
      });
    });
    if (holds) {
      names.push_back(field);
      fields += (fields.empty() ? "" : ", ") + json_quote(field) + ": " +
                json_quote(written_value(field));
    }
  }
  return fields.empty() ? std::string() : "{" + fields + "}";
}

std::string dataset_set_metadata(pdfkit_document* document, std::string_view id,
                                 const DatasetMetadata& metadata, const std::string& out_dir) {
  if (metadata.fields.empty()) {
    return R"({"status": "skipped", "code": "no_metadata"})";
  }
  std::string fields;
  std::vector<std::string> names;
  std::set<std::string> originals;
  for (const auto& [field, values] : metadata.fields) {
    names.push_back(field);
    fields +=
        (fields.empty() ? "" : ", ") + json_quote(field) + ": " + json_quote(written_value(field));
    // The self-check searches an original of three letters or digits or more,
    // so the job searches the page text for those.
    for (const std::string& value : values) {
      const auto letters = std::ranges::count_if(value, [](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return (byte >= '0' && byte <= '9') || ((byte | 0x20U) >= 'a' && (byte | 0x20U) <= 'z') ||
               byte >= 0xC0;
      });
      if (letters >= 3) {
        originals.insert(value);
      }
    }
  }
  fields = "{" + fields + "}";
  const std::vector<std::string> searched(originals.begin(), originals.end());
  std::string matches = R"({"matches": [)";
  for (std::size_t i = 0; i < searched.size(); ++i) {
    matches += (i == 0 ? "" : ", ") + std::string(R"({"search": )") + json_quote(searched[i]) +
               R"(, "placeholder": "[REDACTED]"})";
  }
  matches += "]}";

  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_edit* edit = nullptr;
  Call call = take(pdfkit_edit_new(document, &edit, &out), out);
  if (call.status == PDFKIT_OK && !searched.empty()) {
    call = take(pdfkit_redact_text(edit, matches.c_str(), &out), out);
  }
  if (call.status == PDFKIT_OK) {
    call = take(pdfkit_set_metadata(edit, fields.c_str(), &out), out);
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
    return "{" + dataset_failure(call.status, call.text) + ", \"fields\": " + list(names) +
           ", \"originals\": " + list(searched) + "}";
  }
  std::size_t offset = 0;
  std::string self_check = "null";
  if (const std::optional<JsonText> report = JsonText::parse(call.text, offset)) {
    if (const JsonText::Node* check = report->member(report->root(), "self_check")) {
      if (const JsonText::Node* status = report->member(*check, "status")) {
        self_check = json_quote(status->text);
      }
    }
  }

  // The output read back: every field set carries its written value in the
  // Info dictionary and in every XMP property the output's packet holds for
  // it.
  std::vector<std::string> mismatches;
  std::string xmp_state = "null";
  {
    pdfkit_document* written = nullptr;
    Call opened = take(pdfkit_open(reinterpret_cast<const std::uint8_t*>(pdf.data()), pdf.size(),
                                   nullptr, &written, &out),
                       out);
    Call read = opened;
    if (opened.status == PDFKIT_OK) {
      read = take(pdfkit_metadata(written, nullptr, &out), out);
    }
    pdfkit_close(written);
    const std::optional<JsonText> answer =
        read.status == PDFKIT_OK ? JsonText::parse(read.text, offset) : std::nullopt;
    if (!answer) {
      mismatches = names;
    } else {
      const JsonText::Node* xmp = answer->member(answer->root(), "xmp");
      xmp_state = xmp != nullptr ? std::string(xmp->raw) : "null";
      const bool packet = xmp != nullptr && xmp->text == "read";
      for (const Row& row : rows_of(*answer)) {
        if (std::ranges::find(names, row.field) == names.end()) {
          continue;
        }
        const std::string expected = written_value(row.field);
        bool ok = row.info && *row.info == expected;
        if (packet) {
          ok = ok && !row.xmp.empty() &&
               std::ranges::all_of(
                   row.xmp, [&expected](const std::string& value) { return value == expected; });
        }
        if (!ok) {
          mismatches.push_back(row.field);
        }
      }
    }
  }

  std::string path = out_dir;
  if (!path.empty() && path.back() != '/' && path.back() != '\\') {
    path += '/';
  }
  const std::string relative = "outputs/" + std::string(id) + ".metadata.pdf";
  const auto* data = reinterpret_cast<const std::uint8_t*>(pdf.data());
  if (!write_output_file("dataset", path + relative, data, pdf.size())) {
    return R"({"status": "error", "code": "output_failed", "message": "the output could not be written"})";
  }
  return R"({"status": "ok", "code": null, "fields": )" + list(names) +
         ", \"originals\": " + list(searched) + ", \"output\": " + json_quote(relative) +
         ", \"sha256\": " + json_quote(Sha256::hex(data, pdf.size())) +
         ", \"bytes\": " + std::to_string(pdf.size()) + ", \"self_check\": " + self_check +
         ", \"xmp\": " + xmp_state + ", \"mismatches\": " + list(mismatches) +
         ", \"reopen\": " + dataset_reopen(pdf) + "}";
}

}  // namespace pdfkit::cli::verbs
