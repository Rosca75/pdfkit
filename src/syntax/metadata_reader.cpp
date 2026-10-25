// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The rows, the date normalisation and the agreement test. Two values agree
// when they are equal once leading and trailing white space is set aside;
// dates when their ISO 8601 forms are equal (seconds and time zone filled
// in as written, a fraction of a second dropped, a zero offset read as "Z");
// an author or keyword list when its items, split at commas and semicolons,
// are equal in order, because producers join the XMP array's items with
// either.

#include "syntax/metadata_reader.hpp"

#include "syntax/object.hpp"
#include "syntax/stream.hpp"
#include "syntax/xmp_scanner.hpp"

#include <cctype>
#include <utility>

namespace pdfkit::syntax {

namespace {

constexpr std::array<MetadataReader::Pair, 1> title_pairs = {
    {{XmpScanner::dc, "title", "dc:title"}}};
constexpr std::array<MetadataReader::Pair, 1> author_pairs = {
    {{XmpScanner::dc, "creator", "dc:creator"}}};
constexpr std::array<MetadataReader::Pair, 1> subject_pairs = {
    {{XmpScanner::dc, "description", "dc:description"}}};
constexpr std::array<MetadataReader::Pair, 2> keywords_pairs = {
    {{XmpScanner::pdf, "Keywords", "pdf:Keywords"}, {XmpScanner::dc, "subject", "dc:subject"}}};
constexpr std::array<MetadataReader::Pair, 1> creator_pairs = {
    {{XmpScanner::xmp, "CreatorTool", "xmp:CreatorTool"}}};
constexpr std::array<MetadataReader::Pair, 1> producer_pairs = {
    {{XmpScanner::pdf, "Producer", "pdf:Producer"}}};
constexpr std::array<MetadataReader::Pair, 1> created_pairs = {
    {{XmpScanner::xmp, "CreateDate", "xmp:CreateDate"}}};
constexpr std::array<MetadataReader::Pair, 1> modified_pairs = {
    {{XmpScanner::xmp, "ModifyDate", "xmp:ModifyDate"}}};

std::string_view trim(std::string_view text) {
  const auto space = [](char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  while (!text.empty() && space(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && space(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

std::vector<std::string> items(std::string_view text) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == ',' || text[i] == ';') {
      const std::string_view item = trim(text.substr(start, i - start));
      if (!item.empty()) {
        out.emplace_back(item);
      }
      start = i + 1;
    }
  }
  return out;
}

// Reads n digits at text[at]; advances at.
std::optional<int> digits(std::string_view text, std::size_t& at, std::size_t n) {
  if (at + n > text.size()) {
    return std::nullopt;
  }
  int value = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const char c = text[at + i];
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    value = value * 10 + (c - '0');
  }
  at += n;
  return value;
}

struct Date {
  int year = 0;
  int month = 1;
  int day = 1;
  int hour = 0;
  int minute = 0;
  int second = 0;
  // 'Z', '+', '-' or 0 for no zone.
  char zone = 0;
  int zone_hour = 0;
  int zone_minute = 0;
};

std::string two(int value) {
  std::string out(2, '0');
  out[0] = static_cast<char>('0' + (value / 10) % 10);
  out[1] = static_cast<char>('0' + value % 10);
  return out;
}

bool valid(const Date& date) {
  return date.month >= 1 && date.month <= 12 && date.day >= 1 && date.day <= 31 &&
         date.hour <= 23 && date.minute <= 59 && date.second <= 60 && date.zone_hour <= 23 &&
         date.zone_minute <= 59;
}

std::string iso(Date date) {
  if (date.zone != 0 && date.zone != 'Z' && date.zone_hour == 0 && date.zone_minute == 0) {
    date.zone = 'Z';
  }
  std::string out = std::to_string(date.year);
  while (out.size() < 4) {
    out.insert(out.begin(), '0');
  }
  out += "-" + two(date.month) + "-" + two(date.day) + "T" + two(date.hour) + ":" +
         two(date.minute) + ":" + two(date.second);
  if (date.zone == 'Z') {
    out += "Z";
  } else if (date.zone != 0) {
    out += std::string(1, date.zone) + two(date.zone_hour) + ":" + two(date.zone_minute);
  }
  return out;
}

std::optional<Date> parse_pdf_date(std::string_view text) {
  text = trim(text);
  if (text.starts_with("D:")) {
    text.remove_prefix(2);
  }
  Date date;
  std::size_t at = 0;
  const auto year = digits(text, at, 4);
  if (!year) {
    return std::nullopt;
  }
  date.year = *year;
  for (int* part : {&date.month, &date.day, &date.hour, &date.minute, &date.second}) {
    if (at >= text.size() || text[at] < '0' || text[at] > '9') {
      break;
    }
    const auto value = digits(text, at, 2);
    if (!value) {
      return std::nullopt;
    }
    *part = *value;
  }
  if (at < text.size()) {
    const char zone = text[at];
    if (zone != 'Z' && zone != '+' && zone != '-') {
      return std::nullopt;
    }
    date.zone = zone;
    ++at;
    if (at < text.size() && text[at] >= '0' && text[at] <= '9') {
      const auto hour = digits(text, at, 2);
      if (!hour) {
        return std::nullopt;
      }
      date.zone_hour = *hour;
      if (at < text.size() && text[at] == '\'') {
        ++at;
      }
      if (at < text.size() && text[at] >= '0' && text[at] <= '9') {
        const auto minute = digits(text, at, 2);
        if (!minute) {
          return std::nullopt;
        }
        date.zone_minute = *minute;
      }
      if (at < text.size() && text[at] == '\'') {
        ++at;
      }
    }
    if (at != text.size()) {
      return std::nullopt;
    }
  }
  if (!valid(date)) {
    return std::nullopt;
  }
  return date;
}

std::optional<Date> parse_iso_date(std::string_view text) {
  text = trim(text);
  Date date;
  std::size_t at = 0;
  const auto year = digits(text, at, 4);
  if (!year) {
    return std::nullopt;
  }
  date.year = *year;
  const auto part = [&](char separator, int& value) -> std::optional<bool> {
    if (at >= text.size() || text[at] != separator) {
      return false;
    }
    ++at;
    const auto read = digits(text, at, 2);
    if (!read) {
      return std::nullopt;
    }
    value = *read;
    return true;
  };
  bool more = true;
  for (const auto& [separator, value] :
       {std::pair<char, int*>{'-', &date.month}, std::pair<char, int*>{'-', &date.day},
        std::pair<char, int*>{'T', &date.hour}, std::pair<char, int*>{':', &date.minute},
        std::pair<char, int*>{':', &date.second}}) {
    if (!more) {
      break;
    }
    const auto read = part(separator, *value);
    if (!read) {
      return std::nullopt;
    }
    more = *read;
  }
  if (at < text.size() && text[at] == '.') {
    ++at;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
      ++at;
    }
  }
  if (at < text.size()) {
    const char zone = text[at];
    if (zone != 'Z' && zone != '+' && zone != '-') {
      return std::nullopt;
    }
    date.zone = zone;
    ++at;
    if (zone != 'Z') {
      const auto hour = digits(text, at, 2);
      if (!hour || at >= text.size() || text[at] != ':') {
        return std::nullopt;
      }
      ++at;
      const auto minute = digits(text, at, 2);
      if (!minute) {
        return std::nullopt;
      }
      date.zone_hour = *hour;
      date.zone_minute = *minute;
    }
    if (at != text.size()) {
      return std::nullopt;
    }
  }
  if (!valid(date)) {
    return std::nullopt;
  }
  return date;
}

bool agree(MetadataReader::Field field, const std::string& info, const std::string& xmp) {
  if (field == MetadataReader::Field::author || field == MetadataReader::Field::keywords) {
    return items(info) == items(xmp);
  }
  return trim(info) == trim(xmp);
}

}  // namespace

std::string_view MetadataReader::name(Field field) noexcept {
  switch (field) {
    case Field::title:
      return "Title";
    case Field::author:
      return "Author";
    case Field::subject:
      return "Subject";
    case Field::keywords:
      return "Keywords";
    case Field::creator:
      return "Creator";
    case Field::producer:
      return "Producer";
    case Field::creation_date:
      return "CreationDate";
    case Field::mod_date:
      return "ModDate";
  }
  return "Title";
}

std::optional<MetadataReader::Field> MetadataReader::field(std::string_view name) noexcept {
  for (const Field candidate : fields) {
    if (MetadataReader::name(candidate) == name) {
      return candidate;
    }
  }
  return std::nullopt;
}

std::span<const MetadataReader::Pair> MetadataReader::pairs(Field field) noexcept {
  switch (field) {
    case Field::title:
      return title_pairs;
    case Field::author:
      return author_pairs;
    case Field::subject:
      return subject_pairs;
    case Field::keywords:
      return keywords_pairs;
    case Field::creator:
      return creator_pairs;
    case Field::producer:
      return producer_pairs;
    case Field::creation_date:
      return created_pairs;
    case Field::mod_date:
      return modified_pairs;
  }
  return title_pairs;
}

bool MetadataReader::is_date(Field field) noexcept {
  return field == Field::creation_date || field == Field::mod_date;
}

std::optional<std::string> MetadataReader::pdf_date_to_iso(std::string_view text) {
  const std::optional<Date> date = parse_pdf_date(text);
  if (!date) {
    return std::nullopt;
  }
  return iso(*date);
}

std::optional<std::string> MetadataReader::iso_date(std::string_view text) {
  const std::optional<Date> date = parse_iso_date(text);
  if (!date) {
    return std::nullopt;
  }
  return iso(*date);
}

std::optional<std::string> MetadataReader::iso_to_pdf_date(std::string_view text) {
  std::optional<Date> date = parse_iso_date(text);
  if (!date) {
    return std::nullopt;
  }
  std::string year = std::to_string(date->year);
  while (year.size() < 4) {
    year.insert(year.begin(), '0');
  }
  std::string out = "D:" + year + two(date->month) + two(date->day) + two(date->hour) +
                    two(date->minute) + two(date->second);
  if (date->zone == 'Z' || (date->zone != 0 && date->zone_hour == 0 && date->zone_minute == 0)) {
    out += "Z";
  } else if (date->zone != 0) {
    out += std::string(1, date->zone) + two(date->zone_hour) + "'" + two(date->zone_minute) + "'";
  }
  return out;
}

MetadataReader::Result MetadataReader::read(Document& document) {
  Result result;
  const Object info_value = document.info();
  const Dictionary* info = info_value.dictionary();
  result.info_present = info != nullptr;

  XmpScanner::Result scanned;
  const Object catalog_value = document.catalog();
  if (const Dictionary* catalog = catalog_value.dictionary();
      catalog != nullptr && catalog->contains("Metadata")) {
    result.xmp_present = true;
    const Object metadata = document.resolve_entry(*catalog, "Metadata");
    if (const Stream* stream = metadata.stream()) {
      const Filters::Result decoded = document.decode(*stream);
      if (decoded.damaged || !decoded.remaining.empty()) {
        result.xmp_fault = "the packet's stream does not decode";
      } else {
        result.packet.assign(reinterpret_cast<const char*>(decoded.bytes.data()),
                             decoded.bytes.size());
        scanned = XmpScanner::scan(result.packet);
        if (scanned.readable) {
          result.xmp_readable = true;
        } else {
          result.xmp_fault = scanned.fault;
        }
      }
    } else {
      result.xmp_fault = "the catalog's /Metadata is not a stream";
    }
    if (!result.xmp_readable) {
      document.warn("xmp_unreadable",
                    "the catalog's XMP packet does not scan (" + result.xmp_fault +
                        "), so it is not read and is dropped from any output",
                    std::nullopt);
    }
  }

  for (const Field field : fields) {
    Row row;
    row.field = field;
    if (info != nullptr && info->contains(name(field))) {
      const Object resolved = document.resolve(info->get(name(field)));
      if (const Object::String* string = resolved.string()) {
        PdfDocEncoding::Decoded decoded = PdfDocEncoding::decode(string->bytes);
        row.info = std::move(decoded.text);
        row.info_form = decoded.form;
        if (decoded.form == PdfDocEncoding::Form::utf16le) {
          ++result.utf16le;
        }
      } else if (const std::string* value = resolved.name()) {
        row.info = *value;
      }
    }
    if (result.xmp_readable) {
      for (const Pair& pair : pairs(field)) {
        if (const XmpScanner::Property* property = scanned.find(pair.ns, pair.local)) {
          if (property->form != XmpScanner::Form::structure) {
            row.xmp.push_back({std::string(pair.name), property->value});
          }
        }
      }
    }
    if (is_date(field)) {
      if (row.info) {
        row.info_iso = pdf_date_to_iso(*row.info);
      }
      if (!row.xmp.empty()) {
        row.xmp_iso = iso_date(row.xmp.front().value);
      }
    }
    if (row.info && !row.xmp.empty()) {
      if (is_date(field) && row.info_iso && row.xmp_iso) {
        row.agree = *row.info_iso == *row.xmp_iso;
      } else {
        row.agree = agree(field, *row.info, row.xmp.front().value);
      }
    }
    result.rows.push_back(std::move(row));
  }
  return result;
}

}  // namespace pdfkit::syntax
