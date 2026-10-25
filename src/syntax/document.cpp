// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Opening a document: the header window, the table along the trailer chain
// or by reconstruction, the security handler, the eager parse that surfaces
// every recovery the file needs, and the pages. Resolution is cached per
// object number and guarded against cycles; object streams are opened at
// most eight deep, so a hostile chain of them cannot exhaust the stack.

#include "syntax/document.hpp"

#include "platform/error.hpp"
#include "syntax/page_tree.hpp"
#include "syntax/parser.hpp"
#include "syntax/repair.hpp"

#include <algorithm>
#include <charconv>
#include <ranges>
#include <utility>

namespace pdfkit::syntax {

namespace {

constexpr std::size_t header_window = 1024;
constexpr int max_object_stream_depth = 8;
constexpr int max_reference_chain = 32;

std::string describe(Object::Reference reference) {
  return "object " + std::to_string(reference.number) + " " + std::to_string(reference.generation);
}

// Erases a number from the set of resolutions under way when it goes out of
// scope, whether the resolution returns or throws.
class InProgress {
 public:
  InProgress(std::set<std::uint32_t>& set, std::uint32_t number) : set_(set), number_(number) {
    set_.insert(number_);
  }
  InProgress(const InProgress&) = delete;
  InProgress& operator=(const InProgress&) = delete;
  InProgress(InProgress&&) = delete;
  InProgress& operator=(InProgress&&) = delete;
  ~InProgress() { set_.erase(number_); }

 private:
  std::set<std::uint32_t>& set_;
  std::uint32_t number_;
};

// Counts one more level of object-stream nesting while it lives.
class Nesting {
 public:
  explicit Nesting(int& depth) : depth_(depth) { ++depth_; }
  Nesting(const Nesting&) = delete;
  Nesting& operator=(const Nesting&) = delete;
  Nesting(Nesting&&) = delete;
  Nesting& operator=(Nesting&&) = delete;
  ~Nesting() { --depth_; }

 private:
  int& depth_;
};

// The version number of "1.7", for comparison; no value when malformed.
std::optional<double> version_value(std::string_view text) {
  double value = 0.0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::fixed);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

// The keys of a cross-reference stream's dictionary that describe the stream
// rather than the document, left out of a rebuilt trailer.
bool is_stream_key(std::string_view key) noexcept {
  return key == "Type" || key == "W" || key == "Index" || key == "Length" || key == "Filter" ||
         key == "DecodeParms" || key == "Prev" || key == "XRefStm";
}

}  // namespace

Document::Document(platform::ByteSpan bytes, const Limits& limits)
    : storage_(bytes.begin(), bytes.end()), limits_(limits), budget_(limits.budget()) {
  const std::string_view text(reinterpret_cast<const char*>(storage_.data()), storage_.size());
  const std::size_t found = text.find("%PDF-");
  if (found != std::string_view::npos && found < header_window) {
    header_ = found;
    std::size_t end = found + 5;
    while (end < text.size() && end < found + 12 &&
           ((text[end] >= '0' && text[end] <= '9') || text[end] == '.')) {
      ++end;
    }
    version_ = std::string(text.substr(found + 5, end - found - 5));
  }
  file_ = platform::ByteSpan(storage_).subspan(header_.value_or(0));
}

std::unique_ptr<Document> Document::open(platform::ByteSpan bytes, const Limits& limits) {
  // The constructor is private, so make_unique cannot reach it.
  std::unique_ptr<Document> document(new Document(bytes, limits));
  document->load();
  return document;
}

void Document::repair(std::string reason) {
  if (repair_set_.contains(reason)) {
    return;
  }
  if (repairs_.size() >= max_repairs) {
    ++repairs_dropped_;
    const std::string summary =
        "and " + std::to_string(repairs_dropped_) + " further recoveries of the kinds above";
    if (repairs_dropped_ == 1) {
      repairs_.push_back(summary);
    } else {
      repairs_.back() = summary;
    }
    repair_set_.insert(std::move(reason));
    return;
  }
  repair_set_.insert(reason);
  repairs_.push_back(std::move(reason));
}

void Document::warn(std::string code, std::string message, std::optional<std::uint32_t> page) {
  Warning warning{std::move(code), std::move(message), page};
  if (std::ranges::find(warnings_, warning) == warnings_.end()) {
    warnings_.push_back(std::move(warning));
  }
}

void Document::load() {
  if (!header_) {
    repair("no %PDF- header was found in the first 1,024 bytes");
  }
  std::string failure;
  if (std::optional<CrossReference> table = CrossReference::read(file_, budget_, failure)) {
    entries_ = table->entries();
    trailer_ = table->trailer();
    for (const std::string& note : table->notes()) {
      repair(note);
    }
    failure = check_table();
  }
  if (!failure.empty()) {
    reconstruct(failure);
  } else {
    setup_security();
  }
  parse_all();
  build_pages();
  if (pages_.empty() && !reconstructed_) {
    reconstruct("the page tree yields no page");
    parse_all();
    build_pages();
  }
  if (!reconstructed_) {
    if (const std::string fault = linearization_fault(); !fault.empty()) {
      reconstruct(fault);
      parse_all();
      build_pages();
    }
  }
  if (pages_.empty()) {
    // The tree yields nothing: the page objects the table holds are read in
    // file order.
    std::vector<std::pair<std::pair<std::uint64_t, std::uint32_t>, Object::Reference>> found;
    for (const auto& [number, entry] : entries_) {
      if (entry.type == CrossReference::Entry::Type::free) {
        continue;
      }
      const Object value = object({number, 0});
      const Dictionary* dictionary = value.dictionary();
      if (dictionary != nullptr && dictionary->get("Type").is_name("Page")) {
        // A compressed object is placed at its stream, after the stream itself.
        std::uint64_t at = entry.offset;
        std::uint32_t within = 0;
        if (entry.type == CrossReference::Entry::Type::compressed) {
          const auto holder = entries_.find(static_cast<std::uint32_t>(entry.offset));
          at = holder != entries_.end() ? holder->second.offset : 0;
          within = entry.generation + 1;
        }
        found.push_back({{at, within}, Object::Reference{number, 0}});
      }
    }
    std::ranges::stable_sort(found, {}, [](const auto& item) { return item.first; });
    std::vector<Object::Reference> references;
    references.reserve(found.size());
    for (const auto& item : found) {
      references.push_back(item.second);
    }
    if (!references.empty()) {
      repair("the page tree yields no page; the " + std::to_string(references.size()) +
             " page objects found are read in file order");
      pages_ = PageTree::from_objects(*this, references);
      if (pages_.size() > limits_.page_ceiling) {
        limits_.refuse_page_ceiling(pages_.size());
      }
    }
  }
  if (pages_.empty()) {
    refuse_no_page();
  }
  const Object root = catalog();
  if (const Dictionary* dictionary = root.dictionary()) {
    if (const std::string* later = dictionary->get("Version").name()) {
      const auto later_value = version_value(*later);
      const auto header_value = version_value(version_);
      if (later_value && (!header_value || *later_value > *header_value)) {
        version_ = *later;
      }
    }
  }
}

std::string Document::check_table() {
  const Object& root = trailer_.get("Root");
  if (!root.reference() && root.dictionary() == nullptr) {
    return "the trailer has no /Root";
  }
  if (entries_.size() > limits_.objects) {
    throw platform::Error(
        platform::ErrorCode::limit_exceeded,
        "the cross-reference table names " + std::to_string(entries_.size()) + " objects",
        "at most " + std::to_string(limits_.objects) + " objects",
        "re-save or split the file with its producing application, or raise "
        "objects in the open options");
  }
  const Parser parser(file_);
  for (const auto& [number, entry] : entries_) {
    if (entry.type == CrossReference::Entry::Type::offset) {
      const auto header = entry.offset < file_.size()
                              ? parser.read_header(static_cast<std::size_t>(entry.offset))
                              : std::nullopt;
      if (!header || header->first.number != number) {
        return "the cross-reference offset of object " + std::to_string(number) +
               " does not land on its object";
      }
    } else if (entry.type == CrossReference::Entry::Type::compressed) {
      const auto holder = entries_.find(static_cast<std::uint32_t>(entry.offset));
      if (holder == entries_.end() || holder->second.type != CrossReference::Entry::Type::offset) {
        return "object " + std::to_string(number) + " is said to lie in object stream " +
               std::to_string(entry.offset) + ", which the table does not place";
      }
    }
  }
  return {};
}

Object Document::parse_raw(std::uint32_t number) {
  const auto entry = entries_.find(number);
  if (entry == entries_.end() || entry->second.type != CrossReference::Entry::Type::offset ||
      entry->second.offset >= file_.size()) {
    return {};
  }
  const auto parsed =
      Parser(file_).parse_indirect(static_cast<std::size_t>(entry->second.offset), {});
  return parsed ? parsed->object : Object();
}

void Document::reconstruct(const std::string& reason) {
  repair("the file was reconstructed by scanning it, because " + reason);
  reconstructed_ = true;
  cache_.clear();
  object_streams_ = ObjectStreams();
  security_.reset();
  encrypt_number_.reset();
  pages_.clear();

  const Repair::Found found = Repair::scan(file_);
  entries_.clear();
  for (const auto& [number, location] : found.objects) {
    CrossReference::Entry entry;
    entry.type = CrossReference::Entry::Type::offset;
    entry.offset = location.offset;
    entry.generation = location.generation;
    entries_[number] = entry;
  }
  if (entries_.size() > limits_.objects) {
    throw platform::Error(platform::ErrorCode::limit_exceeded,
                          "the file holds " + std::to_string(entries_.size()) + " objects",
                          "at most " + std::to_string(limits_.objects) + " objects",
                          "re-save or split the file with its producing application, or raise "
                          "objects in the open options");
  }
  // The trailer from every trailer and cross-reference stream found, later
  // over earlier.
  trailer_ = Dictionary();
  for (const Dictionary& candidate : found.trailers) {
    for (const auto& [key, value] : candidate.entries()) {
      if (!is_stream_key(key)) {
        trailer_.set(key, value);
      }
    }
  }
  // Direct objects in file order, for the "last one" choices below.
  std::vector<std::pair<std::uint64_t, std::uint32_t>> order;
  order.reserve(entries_.size());
  for (const auto& [number, entry] : entries_) {
    order.emplace_back(entry.offset, number);
  }
  std::ranges::sort(order);

  // /Encrypt missing while an encryption dictionary is present: the object
  // with /Filter /Standard (MuPDF's repair, technique only).
  if (!trailer_.contains("Encrypt")) {
    for (const auto& item : std::views::reverse(order)) {
      const Object candidate = parse_raw(item.second);
      const Dictionary* dictionary = candidate.dictionary();
      if (dictionary != nullptr && dictionary->get("Filter").is_name("Standard") &&
          dictionary->contains("O") && dictionary->contains("U")) {
        trailer_.set("Encrypt", Object::make_reference({item.second, 0}));
        repair("the trailer's /Encrypt is taken from object " + std::to_string(item.second));
        break;
      }
    }
  }
  setup_security();

  // Every object stream found is opened and indexed; an object inside a stream
  // yields to one outside only when that one is later in the file.
  std::map<std::uint32_t, std::uint64_t> position;
  for (const auto& [offset, number] : order) {
    position[number] = offset;
  }
  for (const auto& [offset, number] : order) {
    const Object value = object({number, entries_[number].generation});
    const Stream* stream = value.stream();
    if (stream == nullptr || !stream->dictionary().get("Type").is_name("ObjStm")) {
      continue;
    }
    const ObjectStreams::Contents* contents = object_stream(number);
    if (contents == nullptr) {
      continue;
    }
    for (std::size_t index = 0; index < contents->objects.size(); ++index) {
      const std::uint32_t held = contents->objects[index].first;
      if (held == number) {
        continue;
      }
      const auto existing = entries_.find(held);
      if (existing != entries_.end() &&
          existing->second.type == CrossReference::Entry::Type::offset && position[held] > offset) {
        continue;
      }
      CrossReference::Entry entry;
      entry.type = CrossReference::Entry::Type::compressed;
      entry.offset = number;
      entry.generation = static_cast<std::uint32_t>(index);
      entries_[held] = entry;
      position[held] = offset;
    }
  }
  cache_.clear();

  // Objects in file order again, now with the compressed ones placed at
  // their stream.
  std::vector<std::pair<std::pair<std::uint64_t, std::uint32_t>, std::uint32_t>> all;
  all.reserve(entries_.size());
  for (const auto& [number, entry] : entries_) {
    all.push_back(
        {{position[number],
          entry.type == CrossReference::Entry::Type::compressed ? entry.generation + 1 : 0},
         number});
  }
  std::ranges::sort(all);
  const auto last_matching = [&](const auto& accept) -> std::optional<std::uint32_t> {
    for (const auto& item : std::views::reverse(all)) {
      const Object candidate = object({item.second, 0});
      const Dictionary* dictionary = candidate.dictionary();
      if (dictionary != nullptr && accept(*dictionary)) {
        return item.second;
      }
    }
    return std::nullopt;
  };
  // /Root missing or wrong: the last object with /Type /Catalog.
  const Object root = resolve(trailer_.get("Root"));
  const Dictionary* root_dictionary = root.dictionary();
  if (root_dictionary == nullptr ||
      (!root_dictionary->get("Type").is_name("Catalog") && !root_dictionary->contains("Pages"))) {
    if (const auto catalog_number = last_matching([](const Dictionary& dictionary) {
          return dictionary.get("Type").is_name("Catalog");
        })) {
      trailer_.set("Root", Object::make_reference({*catalog_number, 0}));
    }
  }
  // /Info missing: the last object with /Producer, or with /Title and no
  // /Type or /Parent (an outline item has a title too).
  if (resolve(trailer_.get("Info")).dictionary() == nullptr) {
    auto info_number =
        last_matching([](const Dictionary& dictionary) { return dictionary.contains("Producer"); });
    if (!info_number) {
      info_number = last_matching([](const Dictionary& dictionary) {
        return dictionary.contains("Title") && !dictionary.contains("Type") &&
               !dictionary.contains("Parent");
      });
    }
    if (info_number) {
      trailer_.set("Info", Object::make_reference({*info_number, 0}));
    }
  }
}

void Document::setup_security() {
  const Object& value = trailer_.get("Encrypt");
  if (value.is_null()) {
    return;
  }
  Dictionary encrypt;
  if (const auto reference = value.reference()) {
    encrypt_number_ = reference->number;
    const Object raw_object = parse_raw(reference->number);
    if (raw_object.dictionary() == nullptr) {
      repair("the trailer's /Encrypt names no dictionary and is ignored");
      return;
    }
    encrypt = *raw_object.dictionary();
  } else if (const Dictionary* direct = value.dictionary()) {
    encrypt = *direct;
  } else {
    repair("the trailer's /Encrypt is not a dictionary and is ignored");
    return;
  }
  std::string id0;
  if (const Object::Array* id = trailer_.get("ID").array(); id != nullptr && !id->empty()) {
    if (const Object::String* first = id->front().string()) {
      id0 = first->bytes;
    }
  }
  security_ = SecurityHandler::open(encrypt, id0);
  if (security_->perms_mismatch()) {
    repair(
        "the encryption dictionary's /Perms does not match its file key; the document is "
        "read all the same");
  }
}

void Document::parse_all() {
  std::vector<std::uint32_t> numbers;
  numbers.reserve(entries_.size());
  for (const auto& [number, entry] : entries_) {
    if (entry.type != CrossReference::Entry::Type::free) {
      numbers.push_back(number);
    }
  }
  for (const std::uint32_t number : numbers) {
    (void)object({number, 0});
  }
}

void Document::build_pages() {
  pages_ = PageTree::walk(*this);
}

std::string Document::linearization_fault() {
  // The first object of a linearized file holds /Linearized; its /O names the
  // first page's object while /L still equals the file's length (ISO 32000-1
  // Annex F.2.2, recalled). An incremental update changes /L's truth, and
  // then the hint is stale by design and not a fault.
  std::optional<std::pair<std::uint64_t, std::uint32_t>> first;
  for (const auto& [number, entry] : entries_) {
    if (entry.type == CrossReference::Entry::Type::offset &&
        (!first || entry.offset < first->first)) {
      first = std::pair{entry.offset, number};
    }
  }
  if (!first || pages_.empty()) {
    return {};
  }
  const Object value = object({first->second, 0});
  const Dictionary* dictionary = value.dictionary();
  if (dictionary == nullptr || !dictionary->contains("Linearized")) {
    return {};
  }
  const auto length = dictionary->get("L").integer();
  const auto first_page = dictionary->get("O").integer();
  if (!length || !first_page ||
      (static_cast<std::uint64_t>(*length) != storage_.size() &&
       static_cast<std::uint64_t>(*length) != file_.size())) {
    return {};
  }
  if (std::cmp_not_equal(*first_page, pages_.front().reference.number)) {
    return "the linearization dictionary names object " + std::to_string(*first_page) +
           " as the first page, but the page tree's first page is object " +
           std::to_string(pages_.front().reference.number);
  }
  return {};
}

void Document::refuse_no_page() const {
  if (!header_) {
    throw platform::Error(platform::ErrorCode::not_pdf,
                          "the bytes are not a PDF: no %PDF- header in the first 1,024 bytes and "
                          "no page object anywhere",
                          "a PDF file, which starts with %PDF- and holds at least one page",
                          "check that the file is a PDF and was not cut short; re-export it "
                          "from the application that made it");
  }
  throw platform::Error(platform::ErrorCode::no_page,
                        "the file has a PDF header but no page object (/Type /Page) was found; "
                        "it is probably truncated",
                        "a PDF file holding at least one page",
                        "re-export the file from the application that produced it");
}

const ObjectStreams::Contents* Document::object_stream(std::uint32_t number) {
  if (const ObjectStreams::Contents* cached = object_streams_.find(number)) {
    return cached;
  }
  if (object_stream_depth_ >= max_object_stream_depth) {
    repair("object streams nest deeper than " + std::to_string(max_object_stream_depth) +
           " at object " + std::to_string(number));
    return nullptr;
  }
  const Nesting guard(object_stream_depth_);
  const Object value = object({number, 0});
  const Stream* stream = value.stream();
  if (stream == nullptr) {
    repair("object " + std::to_string(number) +
           " is named as an object stream but is not a stream");
    return &object_streams_.store(number, {});
  }
  const Filters::Result decoded = decode(*stream);
  ObjectStreams::Contents contents =
      ObjectStreams::parse(decoded.bytes, stream->dictionary(), stream->describe());
  for (const std::string& reason : contents.repairs) {
    repair(reason);
  }
  return &object_streams_.store(number, std::move(contents));
}

Object Document::decrypt_strings(const Object& value, Object::Reference owner) const {
  if (!security_) {
    return value;
  }
  switch (value.kind()) {
    case Object::Kind::string:
      return Object::make_string(security_->decrypt_string(owner, value.string()->bytes),
                                 value.string()->hex);
    case Object::Kind::array: {
      Object::Array items;
      items.reserve(value.array()->size());
      for (const Object& item : *value.array()) {
        items.push_back(decrypt_strings(item, owner));
      }
      return Object::make_array(std::move(items));
    }
    case Object::Kind::dictionary: {
      Dictionary dictionary;
      for (const auto& [key, item] : value.dictionary()->entries()) {
        dictionary.set(key, decrypt_strings(item, owner));
      }
      return Object::make_dictionary(std::move(dictionary));
    }
    case Object::Kind::stream: {
      const Stream& stream = *value.stream();
      const Object dictionary =
          decrypt_strings(Object::make_dictionary(stream.dictionary()), owner);
      return Object::make_stream(Stream(*dictionary.dictionary(), stream.data_offset(),
                                        stream.data_length(), stream.owner()));
    }
    default:
      return value;
  }
}

Object Document::object(Object::Reference reference) {
  const std::uint32_t number = reference.number;
  if (const auto cached = cache_.find(number); cached != cache_.end()) {
    return cached->second;
  }
  const auto found = entries_.find(number);
  if (found == entries_.end() || found->second.type == CrossReference::Entry::Type::free ||
      in_progress_.contains(number)) {
    return {};
  }
  const CrossReference::Entry entry = found->second;
  const InProgress guard(in_progress_, number);
  Object result;
  if (entry.type == CrossReference::Entry::Type::offset) {
    const auto parsed =
        entry.offset < file_.size()
            ? Parser(file_).parse_indirect(
                  static_cast<std::size_t>(entry.offset),
                  [this](Object::Reference length) { return object(length).integer(); })
            : std::nullopt;
    if (!parsed || parsed->reference.number != number) {
      repair(describe(reference) + " could not be read where the table places it");
    } else {
      for (const std::string& reason : parsed->repairs) {
        repair(reason);
      }
      result = parsed->object;
      if (security_ && encrypt_number_ != number) {
        result = decrypt_strings(result, parsed->reference);
      }
    }
  } else {
    const auto holder = static_cast<std::uint32_t>(entry.offset);
    if (const ObjectStreams::Contents* contents = object_stream(holder)) {
      if (const Object* held = ObjectStreams::lookup(*contents, number, entry.generation)) {
        result = *held;
      } else {
        repair("object " + std::to_string(number) + " is not in object stream " +
               std::to_string(holder) + " where the table places it");
      }
    }
  }
  cache_[number] = result;
  return result;
}

Object Document::resolve(const Object& value) {
  Object current = value;
  for (int hop = 0; hop < max_reference_chain; ++hop) {
    const auto reference = current.reference();
    if (!reference) {
      return current;
    }
    current = object(*reference);
  }
  return {};
}

Object Document::resolve_entry(const Dictionary& dictionary, std::string_view key) {
  return resolve(dictionary.get(key));
}

Object Document::catalog() {
  return resolve(trailer_.get("Root"));
}

Object Document::info() {
  return resolve(trailer_.get("Info"));
}

platform::ByteSpan Document::raw(const Stream& stream) const noexcept {
  const std::size_t start = std::min(stream.data_offset(), file_.size());
  const std::size_t length = std::min(stream.data_length(), file_.size() - start);
  return file_.subspan(start, length);
}

Filters::Result Document::decode(const Stream& stream, std::optional<std::uint32_t> page) {
  const Dictionary& dictionary = stream.dictionary();
  if (dictionary.contains("F")) {
    // An external file holds the data; it is never read (CLAUDE.md section 9).
    return {};
  }
  platform::ByteSpan data = raw(stream);
  std::vector<std::byte> clear;
  if (security_) {
    clear = security_->decrypt_stream(stream.owner(), dictionary, data);
    data = clear;
  }
  const Object filter = resolve_entry(dictionary, "Filter");
  Object parameters = resolve_entry(dictionary, "DecodeParms");
  if (const Object::Array* list = parameters.array()) {
    Object::Array resolved;
    for (const Object& item : *list) {
      resolved.push_back(resolve(item));
    }
    parameters = Object::make_array(std::move(resolved));
  }
  Filters::Result result = Filters::decode(data, filter, parameters, budget_, stream.describe());
  if (result.damaged) {
    warn("damaged_stream",
         stream.describe() + ": " + result.fault + "; the stream is read as far as it decodes",
         page);
  }
  return result;
}

std::vector<std::vector<std::byte>> Document::content_pieces(const Object& contents,
                                                             std::uint32_t page) {
  std::vector<std::vector<std::byte>> pieces;
  const Object value = resolve(contents);
  if (const Stream* stream = value.stream()) {
    pieces.push_back(decode(*stream, page).bytes);
  } else if (const Object::Array* list = value.array()) {
    for (const Object& item : *list) {
      const Object piece = resolve(item);
      if (const Stream* piece_stream = piece.stream()) {
        pieces.push_back(decode(*piece_stream, page).bytes);
      }
    }
  }
  return pieces;
}

}  // namespace pdfkit::syntax
