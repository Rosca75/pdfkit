// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The dataset verb: runs every catalogue function this build implements over
// a dataset file and writes one JSON outcome per file, in the shape
// tests/dataset/outcome.md fixes: `--file <pdf> --id <id> --out <dir>` for one
// file, `--manifest <tsv> --files <root> --out <dir>` for every manifest row
// (the file of a row being <root>/<set>/<id>.pdf). An outcome holds no time,
// locale or path, so two runs over one file write the same bytes; it goes to
// <dir>/outcomes/<id>.json, a folder the caller creates, and the outputs of
// the write and the selection to <dir>/outputs/ (dataset_write.cpp). A
// function the verb does not run is null in the outcome.

#include "pdfkit/pdfkit.h"

#include "json_text.hpp"
#include "output.hpp"
#include "sha256.hpp"
#include "support.hpp"
#include "verbs/dataset_images.hpp"
#include "verbs/dataset_metadata.hpp"
#include "verbs/dataset_redact.hpp"
#include "verbs/dataset_redact_images.hpp"
#include "verbs/dataset_text.hpp"
#include "verbs/dataset_write.hpp"
#include "verbs/verbs.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::cli::verbs {

namespace {

// The keys of the outcome after open and inspect, null until their function
// lands, in the order of the catalogue.
constexpr std::string_view later_keys[] = {"text", "images", "metadata", "channels", "redact"};

std::string_view status_name(pdfkit_status status) {
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

// A call's result: its status and the JSON text it returned.
struct Call {
  pdfkit_status status = PDFKIT_E_INTERNAL;
  std::string text;
};

Call take(pdfkit_status status, pdfkit_buffer& out) {
  Call call{status, std::string(reinterpret_cast<const char*>(out.data), out.size)};
  pdfkit_buffer_free(&out);
  return call;
}

// Writes the outcome with two-space indentation, one member per line.
class Writer {
 public:
  void open_object(std::string_view key) {
    member_prefix(key);
    text_ += '{';
    first_ = true;
    ++depth_;
  }
  void close_object() {
    --depth_;
    text_ += "\n" + indent() + "}";
    first_ = false;
  }
  void raw(std::string_view key, std::string_view json) {
    member_prefix(key);
    text_ += json;
  }
  void string(std::string_view key, std::string_view value) { raw(key, json_quote(value)); }
  void strings(std::string_view key, const std::vector<std::string>& values) {
    member_prefix(key);
    if (values.empty()) {
      text_ += "[]";
      return;
    }
    text_ += '[';
    ++depth_;
    for (std::size_t i = 0; i < values.size(); ++i) {
      text_ += (i == 0 ? "\n" : ",\n") + indent() + json_quote(values[i]);
    }
    --depth_;
    text_ += "\n" + indent() + "]";
  }
  [[nodiscard]] std::string finish() { return text_ + "\n"; }

 private:
  void member_prefix(std::string_view key) {
    if (depth_ > 0) {
      text_ += first_ ? "\n" : ",\n";
      text_ += indent();
      text_ += json_quote(key);
      text_ += ": ";
    }
    first_ = false;
  }
  [[nodiscard]] std::string indent() const {
    std::string spaces;
    spaces.assign(depth_ * 2, ' ');
    return spaces;
  }

  std::string text_;
  std::size_t depth_ = 0;
  bool first_ = true;
};

// The status, code and (for an error) message of a call that did not succeed.
void write_failure(Writer& writer, const Call& call) {
  std::size_t offset = 0;
  const std::optional<JsonText> error = JsonText::parse(call.text, offset);
  const JsonText::Node* code = error ? error->member(error->root(), "code") : nullptr;
  if (call.status == PDFKIT_E_REFUSED) {
    writer.string("status", "refused");
    writer.string("code", code != nullptr ? code->text : std::string("refused"));
  } else if (call.status == PDFKIT_E_NOT_IMPLEMENTED) {
    writer.string("status", "not_implemented");
    writer.string("code", "PDFKIT_E_NOT_IMPLEMENTED");
  } else {
    writer.string("status", "error");
    writer.string("code", status_name(call.status));
    const JsonText::Node* failed = error ? error->member(error->root(), "failed") : nullptr;
    writer.string("message", failed != nullptr ? failed->text : call.text);
  }
}

std::vector<std::string> texts(const JsonText& document, const JsonText::Node* array,
                               std::string_view member) {
  std::vector<std::string> out;
  if (array == nullptr) {
    return out;
  }
  for (const std::size_t index : array->items) {
    const JsonText::Node& item = document.node(index);
    if (member.empty()) {
      out.push_back(item.text);
    } else if (const JsonText::Node* field = document.member(item, member)) {
      out.push_back(field->text);
    }
  }
  return out;
}

// The outcome of one file; bytes is empty with missing set for a manifest row
// whose file is absent.
std::string outcome(std::string_view id, const std::vector<std::uint8_t>* bytes,
                    const std::string& out_dir) {
  Writer writer;
  writer.open_object("");
  writer.string("id", id);
  if (bytes == nullptr) {
    writer.raw("sha256", "null");
    writer.raw("bytes", "null");
    writer.open_object("open");
    writer.string("status", "missing");
    writer.raw("code", "null");
    writer.raw("pages", "null");
    writer.strings("repairs", {});
    writer.strings("warnings", {});
    writer.close_object();
    writer.raw("inspect", "null");
    for (const std::string_view key : later_keys) {
      writer.raw(key, "null");
    }
    writer.raw("write", "null");
    writer.raw("select", "null");
    writer.close_object();
    return writer.finish();
  }
  writer.string("sha256", Sha256::hex(bytes->data(), bytes->size()));
  writer.raw("bytes", std::to_string(bytes->size()));

  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_document* document = nullptr;
  const Call opened =
      take(pdfkit_open(bytes->data(), bytes->size(), nullptr, &document, &out), out);
  std::uint64_t page_count = 0;
  writer.open_object("open");
  if (opened.status == PDFKIT_OK) {
    std::size_t offset = 0;
    const std::optional<JsonText> report = JsonText::parse(opened.text, offset);
    const JsonText::Node* pages = report ? report->member(report->root(), "page_count") : nullptr;
    if (pages != nullptr) {
      (void)std::from_chars(pages->raw.data(), pages->raw.data() + pages->raw.size(), page_count);
    }
    writer.string("status", "ok");
    writer.raw("code", "null");
    writer.raw("pages", pages != nullptr ? pages->raw : std::string_view("null"));
    writer.strings("repairs", report ? texts(*report, report->member(report->root(), "repairs"), "")
                                     : std::vector<std::string>{});
    writer.strings("warnings",
                   report ? texts(*report, report->member(report->root(), "warnings"), "code")
                          : std::vector<std::string>{});
  } else {
    write_failure(writer, opened);
    writer.raw("pages", "null");
    writer.strings("repairs", {});
    writer.strings("warnings", {});
  }
  writer.close_object();

  writer.open_object("inspect");
  if (opened.status != PDFKIT_OK) {
    writer.string("status", "skipped");
    writer.raw("code", "null");
    writer.raw("version", "null");
    writer.raw("pages", "null");
  } else {
    const Call inspected = take(pdfkit_inspect(document, nullptr, &out), out);
    if (inspected.status == PDFKIT_OK) {
      std::size_t offset = 0;
      const std::optional<JsonText> report = JsonText::parse(inspected.text, offset);
      const JsonText::Node* version = report ? report->member(report->root(), "version") : nullptr;
      const JsonText::Node* pages = report ? report->member(report->root(), "page_count") : nullptr;
      writer.string("status", "ok");
      writer.raw("code", "null");
      writer.raw("version", version != nullptr ? version->raw : std::string_view("null"));
      writer.raw("pages", pages != nullptr ? pages->raw : std::string_view("null"));
    } else {
      write_failure(writer, inspected);
      writer.raw("version", "null");
      writer.raw("pages", "null");
    }
  }
  writer.close_object();
  const std::string text = opened.status == PDFKIT_OK ? dataset_text(document)
                                                      : R"({"status": "skipped", "code": null})";
  const std::string images = opened.status == PDFKIT_OK ? dataset_images(document)
                                                        : R"({"status": "skipped", "code": null})";
  const DatasetWrite written = opened.status == PDFKIT_OK
                                   ? dataset_write(document, id, page_count, out_dir)
                                   : dataset_write_skipped();
  const DatasetMetadata metadata =
      opened.status == PDFKIT_OK ? dataset_metadata(document)
                                 : DatasetMetadata{R"({"status": "skipped", "code": null})", {}};
  const std::string redact_text = opened.status == PDFKIT_OK
                                      ? dataset_redact(document, id, text, metadata, out_dir)
                                      : R"({"status": "skipped", "code": null})";
  const std::string set_metadata = opened.status == PDFKIT_OK
                                       ? dataset_set_metadata(document, id, metadata, out_dir)
                                       : R"({"status": "skipped", "code": null})";
  const std::string redact_images = opened.status == PDFKIT_OK
                                        ? dataset_redact_images(document, id, out_dir)
                                        : R"({"status": "skipped", "code": null})";
  pdfkit_close(document);
  for (const std::string_view key : later_keys) {
    if (key == "text") {
      writer.raw(key, text);
    } else if (key == "images") {
      writer.raw(key, images);
    } else if (key == "metadata") {
      writer.raw(key, metadata.member);
    } else {
      writer.raw(key, key == "channels" ? std::string_view(written.channels) : "null");
    }
  }
  writer.raw("write", written.write);
  writer.raw("select", written.select);
  writer.raw("redact_text", redact_text);
  writer.raw("set_metadata", set_metadata);
  writer.raw("redact_images", redact_images);
  writer.close_object();
  return writer.finish();
}

std::string outcome_path(const std::string& out_dir, std::string_view id) {
  std::string path = out_dir;
  if (!path.empty() && path.back() != '/' && path.back() != '\\') {
    path += '/';
  }
  return path + "outcomes/" + std::string(id) + ".json";
}

bool write_outcome(const std::string& path, const std::string& text) {
  return write_output_file("dataset", path, reinterpret_cast<const std::uint8_t*>(text.data()),
                           text.size());
}

// Whether path names a file that opens for reading.
bool exists(const std::string& path) {
  const std::unique_ptr<std::FILE, int (*)(std::FILE*)> file(std::fopen(path.c_str(), "rb"),
                                                             &std::fclose);
  return file != nullptr;
}

// The fields of a tab-separated line.
std::vector<std::string> fields_of(std::string_view line) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (true) {
    const std::size_t tab = line.find('\t', start);
    fields.emplace_back(
        line.substr(start, tab == std::string_view::npos ? line.size() - start : tab - start));
    if (tab == std::string_view::npos) {
      return fields;
    }
    start = tab + 1;
  }
}

int run_manifest(const std::string& manifest, const std::string& files, const std::string& out) {
  const std::optional<std::vector<std::uint8_t>> table = read_input_file("dataset", manifest);
  if (!table) {
    return exit_usage;
  }
  const std::string_view text(reinterpret_cast<const char*>(table->data()), table->size());
  std::size_t id_column = std::string_view::npos;
  std::size_t set_column = std::string_view::npos;
  std::size_t rows = 0;
  std::size_t written = 0;
  std::size_t start = 0;
  bool header = true;
  while (start < text.size()) {
    std::size_t end = text.find('\n', start);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    std::string_view line = text.substr(start, end - start);
    start = end + 1;
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    if (line.empty()) {
      continue;
    }
    const std::vector<std::string> fields = fields_of(line);
    if (header) {
      header = false;
      for (std::size_t i = 0; i < fields.size(); ++i) {
        if (fields[i] == "id") {
          id_column = i;
        } else if (fields[i] == "set") {
          set_column = i;
        }
      }
      if (id_column == std::string_view::npos || set_column == std::string_view::npos) {
        return print_usage_error("dataset", "the manifest " + manifest + " has no id or set column",
                                 "a tab-separated manifest whose header names id and set",
                                 "pass dataset/manifest.tsv as --manifest");
      }
      continue;
    }
    if (fields.size() <= std::max(id_column, set_column)) {
      continue;
    }
    ++rows;
    const std::string& id = fields[id_column];
    std::string path = files;
    path += '/';
    path += fields[set_column];
    path += '/';
    path += id;
    path += ".pdf";
    std::string result;
    if (!exists(path)) {
      result = outcome(id, nullptr, out);
    } else {
      const std::optional<std::vector<std::uint8_t>> bytes = read_input_file("dataset", path);
      if (!bytes) {
        return exit_usage;
      }
      result = outcome(id, &*bytes, out);
    }
    if (!write_outcome(outcome_path(out, id), result)) {
      return exit_usage;
    }
    ++written;
  }
  print_json("{\"files\": " + std::to_string(rows) + ", \"written\": " + std::to_string(written) +
             "}");
  return exit_ok;
}

}  // namespace

const Arguments::Spec& dataset_spec() {
  static const Arguments::Spec spec{
      "dataset",
      0,
      0,
      {"manifest", "files", "out", "file", "id"},
      {"out"},
      "dataset --file <file.pdf> --id <id> --out <folder> | dataset --manifest <manifest.tsv> "
      "--files <files root> --out <folder>",
      "dataset --file dataset/files/qpdf/qpdf-0001.pdf --id qpdf-0001 --out dataset/reports/run-1",
  };
  return spec;
}

int run_dataset(std::span<char* const> args) {
  const std::optional<Arguments> parsed = Arguments::parse(args, dataset_spec());
  if (!parsed) {
    return exit_usage;
  }
  const std::optional<std::string> manifest = parsed->option("manifest");
  const std::optional<std::string> files = parsed->option("files");
  const std::optional<std::string> file = parsed->option("file");
  const std::optional<std::string> id = parsed->option("id");
  const std::optional<std::string> out = parsed->option("out");
  const bool manifest_run = manifest && files && !file && !id;
  const bool file_run = file && id && !manifest && !files;
  if (manifest_run == file_run || !out) {
    return print_usage_error(
        "dataset", "the options name neither a manifest run nor a single-file run, or both",
        "pdfkit " + std::string(dataset_spec().usage),
        "for example: pdfkit " + std::string(dataset_spec().example));
  }
  if (const std::optional<int> missing = require_implemented({"pdfkit_open", "pdfkit_inspect"})) {
    return *missing;
  }
  if (manifest && files && out) {
    return run_manifest(*manifest, *files, *out);
  }
  if (!file || !id || !out) {
    return exit_usage;
  }
  const std::optional<std::vector<std::uint8_t>> bytes = read_input_file("dataset", *file);
  if (!bytes) {
    return exit_usage;
  }
  const std::string path = outcome_path(*out, *id);
  if (!write_outcome(path, outcome(*id, &*bytes, *out))) {
    return exit_usage;
  }
  print_json("{\"id\": " + json_quote(*id) + ", \"outcome\": " + json_quote(path) + "}");
  return exit_ok;
}

}  // namespace pdfkit::cli::verbs
