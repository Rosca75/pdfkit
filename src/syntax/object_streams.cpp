// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The parsing of an object stream's header and objects, and the per-stream
// cache. The header's count is bounded by the decoded bytes, so a hostile /N
// reserves nothing.

#include "syntax/object_streams.hpp"

#include "syntax/lexer.hpp"
#include "syntax/parser.hpp"

#include <algorithm>
#include <utility>

namespace pdfkit::syntax {

ObjectStreams::Contents ObjectStreams::parse(platform::ByteSpan decoded,
                                             const Dictionary& dictionary, std::string_view name) {
  Contents contents;
  const std::string prefix = std::string(name) + " (an object stream): ";
  const auto count = dictionary.get("N").integer();
  const auto first = dictionary.get("First").integer();
  if (!count || !first || *count < 0 || *first < 0 ||
      static_cast<std::uint64_t>(*first) > decoded.size()) {
    contents.repairs.push_back(prefix + "/N or /First is missing or out of range");
    return contents;
  }
  // Each header pair takes at least four bytes ("1 2 "), which bounds /N.
  const std::uint64_t most = decoded.size() / 2;
  const std::uint64_t pairs = std::min(static_cast<std::uint64_t>(*count), most);
  Lexer header(decoded);
  std::vector<std::pair<std::uint32_t, std::uint64_t>> offsets;
  for (std::uint64_t i = 0; i < pairs; ++i) {
    const Lexer::Token number = header.next();
    const Lexer::Token offset = header.next();
    if (number.kind != Lexer::TokenKind::integer || offset.kind != Lexer::TokenKind::integer ||
        !std::in_range<std::uint32_t>(number.integer) || offset.integer < 0) {
      contents.repairs.push_back(prefix + "the header ends after " + std::to_string(i) + " of " +
                                 std::to_string(*count) + " pairs");
      break;
    }
    offsets.emplace_back(static_cast<std::uint32_t>(number.integer),
                         static_cast<std::uint64_t>(offset.integer));
  }
  const auto base = static_cast<std::uint64_t>(*first);
  for (const auto& [number, offset] : offsets) {
    if (offset > decoded.size() - base) {
      contents.repairs.push_back(prefix + "object " + std::to_string(number) +
                                 " lies past the end of the stream");
      contents.objects.emplace_back(number, Object());
      continue;
    }
    Lexer lexer(decoded, static_cast<std::size_t>(base + offset));
    const Lexer::Token token = lexer.next();
    std::vector<std::string> faults;
    Object object = Parser::parse_value(lexer, token, true, faults);
    for (const std::string& fault : faults) {
      std::string reason = prefix;
      reason += "object ";
      reason += std::to_string(number);
      reason += ": ";
      reason += fault;
      contents.repairs.push_back(std::move(reason));
    }
    contents.objects.emplace_back(number, std::move(object));
  }
  return contents;
}

const ObjectStreams::Contents* ObjectStreams::find(std::uint32_t stream) const noexcept {
  const auto found = cache_.find(stream);
  return found == cache_.end() ? nullptr : &found->second;
}

const ObjectStreams::Contents& ObjectStreams::store(std::uint32_t stream, Contents contents) {
  return cache_.insert_or_assign(stream, std::move(contents)).first->second;
}

const Object* ObjectStreams::lookup(const Contents& contents, std::uint32_t number,
                                    std::uint32_t index) noexcept {
  if (index < contents.objects.size() && contents.objects[index].first == number) {
    return &contents.objects[index].second;
  }
  for (const auto& [held, object] : contents.objects) {
    if (held == number) {
      return &object;
    }
  }
  return nullptr;
}

}  // namespace pdfkit::syntax
