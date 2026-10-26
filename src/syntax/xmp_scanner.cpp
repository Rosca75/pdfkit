// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// One pass over the packet's bytes: processing instructions, comments,
// CDATA sections and a DOCTYPE are stepped over (XML 1.0, recalled), start
// and end tags are matched on a stack whose frames carry the namespace
// declarations they make, and each frame knows its role (the rdf:RDF
// element, a description, a property, an array, an item, or something
// deeper), which is all the RDF/XML of an XMP packet needs.

#include "syntax/xmp_scanner.hpp"

#include "platform/utf8_sequence.hpp"

#include <charconv>
#include <utility>

namespace pdfkit::syntax {

namespace {

// Nested elements past this depth make the packet unreadable, as a bound on
// a packet built to exhaust the stack frames' memory.
constexpr std::size_t max_depth = 256;

enum class Role : std::uint8_t { other, rdf_root, description, property, array, item, inner };

struct Binding {
  std::string prefix;
  std::string uri;
};

struct Attribute {
  std::string qname;
  std::size_t name_begin = 0;
  std::size_t value_begin = 0;
  std::size_t value_end = 0;
  std::string value;
};

struct Frame {
  std::string qname;
  Role role = Role::other;
  std::size_t bindings = 0;
  std::size_t begin = 0;
  std::size_t content = 0;
  // The property this frame belongs to, as an index into the result.
  std::size_t property = 0;
  bool has_child = false;
  std::string lang;
};

bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool is_name_char(char c) {
  return !is_space(c) && c != '>' && c != '/' && c != '=' && c != '<' && c != '"' && c != '\'';
}

class Scanner {
 public:
  explicit Scanner(std::string_view packet) : packet_(packet) {}

  XmpScanner::Result run();

 private:
  bool fail(std::string fault) {
    result_.readable = false;
    result_.fault = std::move(fault);
    return false;
  }

  [[nodiscard]] std::string uri_of(std::string_view prefix) const {
    for (auto it = bindings_.rbegin(); it != bindings_.rend(); ++it) {
      if (it->prefix == prefix) {
        return it->uri;
      }
    }
    if (prefix == "xml") {
      return std::string(XmpScanner::xml);
    }
    return {};
  }

  // The namespace and local name of a qualified name; unprefixed attributes
  // have no namespace, unprefixed elements take the default one.
  [[nodiscard]] std::pair<std::string, std::string> resolve(std::string_view qname,
                                                            bool element) const {
    const std::size_t colon = qname.find(':');
    if (colon == std::string_view::npos) {
      return {element ? uri_of("") : std::string(), std::string(qname)};
    }
    return {uri_of(qname.substr(0, colon)), std::string(qname.substr(colon + 1))};
  }

  [[nodiscard]] bool is_rdf(std::string_view qname, std::string_view local, bool element) const {
    const auto [ns, name] = resolve(qname, element);
    return ns == XmpScanner::rdf && name == local;
  }

  bool skip_to(std::string_view terminator, std::string_view what) {
    const std::size_t end = packet_.find(terminator, at_);
    if (end == std::string_view::npos) {
      return fail("the packet has " + std::string(what) + " that does not end");
    }
    at_ = end + terminator.size();
    return true;
  }

  bool tag();
  bool start_tag();
  bool end_tag();
  void open(Frame frame, const std::vector<Attribute>& attributes, bool empty);
  void close(const Frame& frame, std::size_t end_begin, std::size_t end);

  std::string_view packet_;
  std::size_t at_ = 0;
  std::vector<Frame> stack_;
  std::vector<Binding> bindings_;
  XmpScanner::Result result_;
  bool saw_rdf_ = false;
};

XmpScanner::Result Scanner::run() {
  if (packet_.size() >= 2 && ((packet_[0] == '\xFE' && packet_[1] == '\xFF') ||
                              (packet_[0] == '\xFF' && packet_[1] == '\xFE') ||
                              packet_[0] == '\0' || packet_[1] == '\0')) {
    fail("the packet is not UTF-8 (a UTF-16 or UTF-32 packet is not read)");
    return result_;
  }
  bool blank = true;
  for (const char c : packet_) {
    blank = blank && (is_space(c) || c == '\0');
  }
  if (blank) {
    return result_;
  }
  while (at_ < packet_.size()) {
    const std::size_t next = packet_.find('<', at_);
    if (next == std::string_view::npos) {
      break;
    }
    at_ = next;
    if (!tag()) {
      return result_;
    }
  }
  if (!stack_.empty()) {
    fail("the element <" + stack_.back().qname + "> is never closed");
    return result_;
  }
  if (!saw_rdf_) {
    fail("the packet holds no rdf:RDF element");
  }
  return result_;
}

bool Scanner::tag() {
  const std::string_view rest = packet_.substr(at_);
  if (rest.starts_with("<?")) {
    return skip_to("?>", "a processing instruction");
  }
  if (rest.starts_with("<!--")) {
    return skip_to("-->", "a comment");
  }
  if (rest.starts_with("<![CDATA[")) {
    if (!stack_.empty()) {
      stack_.back().has_child = stack_.back().has_child || stack_.back().role == Role::item ||
                                stack_.back().role == Role::property;
    }
    return skip_to("]]>", "a CDATA section");
  }
  if (rest.starts_with("<!")) {
    return skip_to(">", "a declaration");
  }
  if (rest.starts_with("</")) {
    return end_tag();
  }
  return start_tag();
}

bool Scanner::start_tag() {
  const std::size_t begin = at_;
  std::size_t i = at_ + 1;
  while (i < packet_.size() && is_name_char(packet_[i])) {
    ++i;
  }
  if (i == at_ + 1) {
    return fail("the packet has a \"<\" that starts no element");
  }
  Frame frame;
  frame.qname = std::string(packet_.substr(at_ + 1, i - at_ - 1));
  frame.begin = begin;
  std::vector<Attribute> attributes;
  bool empty = false;
  while (true) {
    while (i < packet_.size() && is_space(packet_[i])) {
      ++i;
    }
    if (i >= packet_.size()) {
      return fail("the start tag <" + frame.qname + "> does not end");
    }
    if (packet_[i] == '>') {
      ++i;
      break;
    }
    if (packet_[i] == '/' && i + 1 < packet_.size() && packet_[i + 1] == '>') {
      empty = true;
      i += 2;
      break;
    }
    Attribute attribute;
    attribute.name_begin = i;
    while (i < packet_.size() && is_name_char(packet_[i])) {
      ++i;
    }
    attribute.qname = std::string(packet_.substr(attribute.name_begin, i - attribute.name_begin));
    while (i < packet_.size() && is_space(packet_[i])) {
      ++i;
    }
    if (attribute.qname.empty() || i >= packet_.size() || packet_[i] != '=') {
      return fail("an attribute of <" + frame.qname + "> has no value");
    }
    ++i;
    while (i < packet_.size() && is_space(packet_[i])) {
      ++i;
    }
    if (i >= packet_.size() || (packet_[i] != '"' && packet_[i] != '\'')) {
      return fail("the attribute " + attribute.qname + " of <" + frame.qname +
                  "> has no quoted value");
    }
    const char quote = packet_[i];
    const std::size_t close = packet_.find(quote, i + 1);
    if (close == std::string_view::npos) {
      return fail("the value of the attribute " + attribute.qname + " of <" + frame.qname +
                  "> has no closing quote");
    }
    attribute.value_begin = i + 1;
    attribute.value_end = close;
    attribute.value = XmpScanner::unescape(packet_.substr(i + 1, close - i - 1));
    attributes.push_back(std::move(attribute));
    i = close + 1;
  }
  at_ = i;
  frame.content = i;
  if (stack_.size() >= max_depth) {
    return fail("the packet nests elements deeper than " + std::to_string(max_depth));
  }
  // The declarations of this element are in scope for its own name.
  for (const Attribute& attribute : attributes) {
    if (attribute.qname == "xmlns") {
      bindings_.push_back({"", attribute.value});
      ++frame.bindings;
    } else if (attribute.qname.starts_with("xmlns:")) {
      bindings_.push_back({attribute.qname.substr(6), attribute.value});
      ++frame.bindings;
    }
  }
  open(std::move(frame), attributes, empty);
  return true;
}

void Scanner::open(Frame frame, const std::vector<Attribute>& attributes, bool empty) {
  const Role parent = stack_.empty() ? Role::other : stack_.back().role;
  if (!stack_.empty()) {
    stack_.back().has_child = true;
  }
  const auto attribute = [&](std::string_view local) -> const Attribute* {
    for (const Attribute& candidate : attributes) {
      if (is_rdf(candidate.qname, local, false)) {
        return &candidate;
      }
    }
    return nullptr;
  };
  std::vector<XmpScanner::Property>& properties = result_.properties;
  switch (parent) {
    case Role::other:
      for (const Attribute& candidate : attributes) {
        if (candidate.qname == "xmlns" || candidate.qname.starts_with("xmlns:")) {
          continue;
        }
        XmpScanner::Property other;
        std::tie(other.ns, other.local) = resolve(candidate.qname, false);
        other.qname = candidate.qname;
        other.form = XmpScanner::Form::attribute;
        other.begin = candidate.name_begin;
        other.end = candidate.value_end + 1;
        other.value_begin = candidate.value_begin;
        other.value_end = candidate.value_end;
        other.value = candidate.value;
        result_.others.push_back(std::move(other));
      }
      frame.role = is_rdf(frame.qname, "RDF", true) ? Role::rdf_root : Role::other;
      if (frame.role == Role::rdf_root) {
        saw_rdf_ = true;
        const std::size_t colon = frame.qname.find(':');
        result_.rdf_prefix = colon == std::string::npos ? "" : frame.qname.substr(0, colon);
      }
      break;
    case Role::rdf_root: {
      // An rdf:Description, or a typed node, which XMP does not write but
      // which holds properties alike.
      frame.role = Role::description;
      for (const Attribute& candidate : attributes) {
        if (candidate.qname == "xmlns" || candidate.qname.starts_with("xmlns:")) {
          continue;
        }
        auto [ns, local] = resolve(candidate.qname, false);
        if (ns.empty() || ns == XmpScanner::rdf || ns == XmpScanner::xml) {
          continue;
        }
        XmpScanner::Property property;
        property.ns = std::move(ns);
        property.local = std::move(local);
        property.qname = candidate.qname;
        property.form = XmpScanner::Form::attribute;
        property.begin = candidate.name_begin;
        property.end = candidate.value_end + 1;
        property.value_begin = candidate.value_begin;
        property.value_end = candidate.value_end;
        property.value = candidate.value;
        properties.push_back(std::move(property));
      }
      break;
    }
    case Role::description: {
      frame.role = Role::property;
      XmpScanner::Property property;
      std::tie(property.ns, property.local) = resolve(frame.qname, true);
      property.qname = frame.qname;
      property.form = XmpScanner::Form::simple;
      property.begin = frame.begin;
      property.value_begin = frame.content;
      property.value_end = frame.content;
      if (attribute("parseType") != nullptr || attribute("resource") != nullptr) {
        property.form = XmpScanner::Form::structure;
      }
      for (const Attribute& candidate : attributes) {
        if (candidate.qname == "xmlns" || candidate.qname.starts_with("xmlns:")) {
          continue;
        }
        const auto [ns, local] = resolve(candidate.qname, false);
        if (ns != XmpScanner::xml && ns != XmpScanner::rdf) {
          // A qualifier written as an attribute makes a structure.
          property.form = XmpScanner::Form::structure;
        }
      }
      frame.property = properties.size();
      properties.push_back(std::move(property));
      break;
    }
    case Role::property: {
      XmpScanner::Property& property = properties[stack_.back().property];
      frame.property = stack_.back().property;
      frame.role = Role::inner;
      if (property.form == XmpScanner::Form::simple) {
        if (is_rdf(frame.qname, "Alt", true)) {
          property.form = XmpScanner::Form::alt;
          frame.role = Role::array;
        } else if (is_rdf(frame.qname, "Seq", true)) {
          property.form = XmpScanner::Form::seq;
          frame.role = Role::array;
        } else if (is_rdf(frame.qname, "Bag", true)) {
          property.form = XmpScanner::Form::bag;
          frame.role = Role::array;
        }
      }
      if (frame.role == Role::inner) {
        property.form = XmpScanner::Form::structure;
        property.items.clear();
      }
      break;
    }
    case Role::array: {
      XmpScanner::Property& property = properties[stack_.back().property];
      frame.property = stack_.back().property;
      frame.role = Role::inner;
      if (is_rdf(frame.qname, "li", true) && attribute("parseType") == nullptr &&
          attribute("resource") == nullptr) {
        frame.role = Role::item;
        for (const Attribute& candidate : attributes) {
          const auto [ns, local] = resolve(candidate.qname, false);
          if (ns == XmpScanner::xml && local == "lang") {
            frame.lang = candidate.value;
          }
        }
      } else {
        property.form = XmpScanner::Form::structure;
        property.items.clear();
      }
      break;
    }
    case Role::item:
    case Role::inner: {
      frame.property = stack_.back().property;
      frame.role = Role::inner;
      XmpScanner::Property& property = properties[frame.property];
      property.form = XmpScanner::Form::structure;
      property.items.clear();
      break;
    }
  }
  stack_.push_back(std::move(frame));
  if (empty) {
    const Frame closed = std::move(stack_.back());
    stack_.pop_back();
    bindings_.resize(bindings_.size() - closed.bindings);
    close(closed, at_, at_);
  }
}

bool Scanner::end_tag() {
  const std::size_t begin = at_;
  const std::size_t close_at = packet_.find('>', at_);
  if (close_at == std::string_view::npos) {
    return fail("an end tag of the packet does not end");
  }
  std::string_view name = packet_.substr(at_ + 2, close_at - at_ - 2);
  while (!name.empty() && is_space(name.back())) {
    name.remove_suffix(1);
  }
  if (stack_.empty()) {
    return fail("the end tag </" + std::string(name) + "> closes no element");
  }
  if (name != stack_.back().qname) {
    return fail("the end tag </" + std::string(name) + "> closes <" + stack_.back().qname + ">");
  }
  at_ = close_at + 1;
  const Frame frame = std::move(stack_.back());
  stack_.pop_back();
  if (frame.role == Role::rdf_root && !result_.rdf_end) {
    result_.rdf_end = begin;
  }
  close(frame, begin, at_);
  bindings_.resize(bindings_.size() - frame.bindings);
  return true;
}

void Scanner::close(const Frame& frame, std::size_t end_begin, std::size_t end) {
  std::vector<XmpScanner::Property>& properties = result_.properties;
  if (frame.role == Role::item) {
    XmpScanner::Property& property = properties[frame.property];
    if (property.form == XmpScanner::Form::structure) {
      return;
    }
    if (frame.has_child) {
      property.form = XmpScanner::Form::structure;
      property.items.clear();
      return;
    }
    XmpScanner::Item item;
    item.begin = frame.content;
    item.end = end_begin;
    item.text = XmpScanner::unescape(packet_.substr(frame.content, end_begin - frame.content));
    item.lang = frame.lang;
    property.items.push_back(std::move(item));
    return;
  }
  if (frame.role != Role::property) {
    return;
  }
  XmpScanner::Property& property = properties[frame.property];
  property.end = end;
  property.value_begin = frame.content;
  property.value_end = end_begin;
  switch (property.form) {
    case XmpScanner::Form::simple:
      if (frame.has_child) {
        property.form = XmpScanner::Form::structure;
      } else {
        property.value =
            XmpScanner::unescape(packet_.substr(frame.content, end_begin - frame.content));
      }
      break;
    case XmpScanner::Form::alt: {
      const XmpScanner::Item* chosen = property.items.empty() ? nullptr : &property.items.front();
      for (const XmpScanner::Item& item : property.items) {
        if (item.lang == "x-default") {
          chosen = &item;
          break;
        }
      }
      property.value = chosen == nullptr ? std::string() : chosen->text;
      break;
    }
    case XmpScanner::Form::seq:
    case XmpScanner::Form::bag:
      for (std::size_t i = 0; i < property.items.size(); ++i) {
        property.value += (i == 0 ? "" : "; ") + property.items[i].text;
      }
      break;
    case XmpScanner::Form::attribute:
    case XmpScanner::Form::structure:
      break;
  }
}

void append_reference(std::string& out, std::string_view digits, int base) {
  std::uint32_t value = 0;
  const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value, base);
  if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() || value == 0 ||
      value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
    out += "\xEF\xBF\xBD";
    return;
  }
  platform::append_utf8(out, static_cast<char32_t>(value));
}

}  // namespace

const XmpScanner::Property* XmpScanner::Result::find(std::string_view ns,
                                                     std::string_view local) const {
  for (const Property& property : properties) {
    if (property.ns == ns && property.local == local) {
      return &property;
    }
  }
  return nullptr;
}

XmpScanner::Result XmpScanner::scan(std::string_view packet) {
  Scanner scanner(packet);
  return scanner.run();
}

std::string XmpScanner::unescape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  std::size_t i = 0;
  while (i < text.size()) {
    if (text[i] != '&') {
      out += text[i++];
      continue;
    }
    const std::size_t semicolon = text.find(';', i);
    if (semicolon == std::string_view::npos || semicolon - i > 12) {
      out += text[i++];
      continue;
    }
    const std::string_view name = text.substr(i + 1, semicolon - i - 1);
    if (name == "lt") {
      out += '<';
    } else if (name == "gt") {
      out += '>';
    } else if (name == "amp") {
      out += '&';
    } else if (name == "quot") {
      out += '"';
    } else if (name == "apos") {
      out += '\'';
    } else if (name.starts_with("#x") || name.starts_with("#X")) {
      append_reference(out, name.substr(2), 16);
    } else if (name.starts_with("#")) {
      append_reference(out, name.substr(1), 10);
    } else {
      out.append(text.substr(i, semicolon - i + 1));
    }
    i = semicolon + 1;
  }
  return out;
}

std::string XmpScanner::escape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      case '\'':
        out += "&apos;";
        break;
      default:
        out += c;
    }
  }
  return out;
}

}  // namespace pdfkit::syntax
