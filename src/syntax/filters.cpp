// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The non-image filters of ISO 32000-1 section 7.4 (read) and the predictors
// of section 7.4.4.4 with the PNG filter types of the PNG specification
// (W3C PNG, section 9, recalled; tested against hand-encoded rows). Each
// filter writes into a platform::Buffer, so its output is charged to the
// budget as it grows. LZW follows the specification's code-width rules with
// /EarlyChange; pdfminer's lzw.py served as a size reference only.

#include "syntax/filters.hpp"

#include "platform/buffer.hpp"
#include "platform/checked.hpp"
#include "syntax/dictionary.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <utility>

namespace pdfkit::syntax {

namespace {

using platform::Buffer;
using platform::ByteSpan;

struct Stage {
  std::vector<std::byte> bytes;
  bool damaged = false;
  std::string fault;
};

[[nodiscard]] unsigned byte_at(ByteSpan data, std::size_t index) noexcept {
  return std::to_integer<unsigned>(data[index]);
}

[[nodiscard]] bool is_white(unsigned value) noexcept {
  return value == 0x00 || value == 0x09 || value == 0x0A || value == 0x0C || value == 0x0D ||
         value == 0x20;
}

Stage finish(Buffer&& buffer, std::string fault = {}) {
  Stage stage;
  stage.damaged = !fault.empty();
  stage.fault = std::move(fault);
  stage.bytes = std::move(buffer).release();
  return stage;
}

// FlateDecode: a zlib header (RFC 1950, recalled) is checked and skipped, and
// the deflate data (RFC 1951) is inflated raw, so data with no header at all
// still inflates and a missing Adler-32 is not a fault (viewers do not check
// it). A present Adler-32 that disagrees with the decoded bytes marks the
// stream damaged with every byte kept, because a flipped byte inside a
// literal inflates without any other sign.
Stage flate(ByteSpan data, platform::Budget& budget, std::string_view object) {
  Buffer out(budget, std::string(object));
  if (data.empty()) {
    return finish(std::move(out));
  }
  std::size_t start = 0;
  if (data.size() >= 2) {
    const unsigned cmf = byte_at(data, 0);
    const unsigned flg = byte_at(data, 1);
    if ((cmf & 0x0FU) == 8 && (cmf >> 4U) <= 7 && ((cmf << 8U) | flg) % 31 == 0) {
      if ((flg & 0x20U) != 0) {
        return finish(std::move(out),
                      "the Flate data names a preset dictionary, which PDF "
                      "does not define");
      }
      start = 2;
    }
  }
  z_stream stream{};
  if (inflateInit2(&stream, -15) != Z_OK) {
    return finish(std::move(out), "zlib could not start inflating");
  }
  std::vector<unsigned char> chunk(65536);
  uLong checksum = adler32(0L, Z_NULL, 0);
  std::size_t fed = start;
  std::string fault;
  while (true) {
    if (stream.avail_in == 0 && fed < data.size()) {
      const std::size_t piece = std::min<std::size_t>(data.size() - fed, 1U << 30U);
      // zlib takes a non-const pointer and never writes through it.
      stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data.data() + fed));
      stream.avail_in = static_cast<uInt>(piece);
      fed += piece;
    }
    stream.next_out = chunk.data();
    stream.avail_out = static_cast<uInt>(chunk.size());
    const int result = inflate(&stream, Z_NO_FLUSH);
    const std::size_t produced = chunk.size() - stream.avail_out;
    try {
      out.append(std::as_bytes(std::span(chunk.data(), produced)));
      checksum = adler32(checksum, chunk.data(), static_cast<uInt>(produced));
    } catch (...) {
      inflateEnd(&stream);
      throw;
    }
    if (result == Z_STREAM_END) {
      const std::size_t end = fed - stream.avail_in;
      if (start == 2 && data.size() - end >= 4) {
        const auto stored =
            static_cast<uLong>(platform::read_be<std::uint32_t>(data, end).value_or(0));
        if (stored != checksum) {
          fault = "the Flate data's Adler-32 checksum does not match its decoded bytes";
        }
      }
      break;
    }
    if (result == Z_OK) {
      continue;
    }
    if (result == Z_BUF_ERROR && stream.avail_in == 0 && fed >= data.size()) {
      fault = "the Flate data ends before its last block";
    } else if (result != Z_BUF_ERROR) {
      fault = std::string("the Flate data is corrupt (zlib: ") +
              (stream.msg != nullptr ? stream.msg : "error") + ")";
    } else {
      continue;
    }
    break;
  }
  if (!fault.empty()) {
    fault += " (" + std::to_string(out.size()) + " bytes decoded)";
  }
  inflateEnd(&stream);
  return finish(std::move(out), fault);
}

// LZWDecode, ISO 32000-1 section 7.4.4.2: codes of 9 to 12 bits, 256 clears
// the table, 257 ends the data; with /EarlyChange 1 (the default) the code
// width grows one code early.
Stage lzw(ByteSpan data, int early_change, platform::Budget& budget, std::string_view object) {
  Buffer out(budget, std::string(object));
  constexpr std::size_t table_size = 4096;
  std::vector<std::uint16_t> prefix(table_size, 0);
  std::vector<std::uint8_t> suffix(table_size, 0);
  std::vector<std::uint16_t> length(table_size, 0);
  for (unsigned i = 0; i < 256; ++i) {
    suffix[i] = static_cast<std::uint8_t>(i);
    length[i] = 1;
  }
  std::vector<std::byte> entry;
  const auto first_byte = [&](unsigned code) {
    while (length[code] > 1) {
      code = prefix[code];
    }
    return suffix[code];
  };
  const auto emit = [&](unsigned code) {
    entry.resize(length[code]);
    for (std::size_t i = length[code]; i > 0; --i) {
      entry[i - 1] = std::byte{suffix[code]};
      code = prefix[code];
    }
    out.append(entry);
  };
  unsigned width = 9;
  unsigned next = 258;
  int previous = -1;
  std::uint32_t bits = 0;
  unsigned held = 0;
  std::size_t at = 0;
  const auto early = static_cast<unsigned>(early_change != 0 ? 1 : 0);
  while (true) {
    while (held < width && at < data.size()) {
      bits = (bits << 8U) | byte_at(data, at++);
      held += 8;
    }
    if (held < width) {
      break;
    }
    const unsigned code = (bits >> (held - width)) & ((1U << width) - 1U);
    held -= width;
    bits &= (1U << held) - 1U;
    if (code == 256) {
      width = 9;
      next = 258;
      previous = -1;
      continue;
    }
    if (code == 257) {
      break;
    }
    if (previous < 0) {
      if (code > 255) {
        return finish(std::move(out), "the LZW data starts with an undefined code");
      }
      emit(code);
      previous = static_cast<int>(code);
      continue;
    }
    const auto prior = static_cast<unsigned>(previous);
    std::uint8_t first = 0;
    if (code < next) {
      first = first_byte(code);
    } else if (code == next) {
      first = first_byte(prior);
    } else {
      return finish(std::move(out), "the LZW data holds an undefined code");
    }
    if (next < table_size) {
      prefix[next] = static_cast<std::uint16_t>(prior);
      suffix[next] = first;
      length[next] = static_cast<std::uint16_t>(std::min<unsigned>(length[prior] + 1U, 4096U));
      ++next;
    }
    emit(code);
    previous = static_cast<int>(code);
    if (next + early >= 2048) {
      width = 12;
    } else if (next + early >= 1024) {
      width = 11;
    } else if (next + early >= 512) {
      width = 10;
    }
  }
  return finish(std::move(out));
}

// ASCIIHexDecode, ISO 32000-1 section 7.4.2.
Stage ascii_hex(ByteSpan data, platform::Budget& budget, std::string_view object) {
  Buffer out(budget, std::string(object));
  int pending = -1;
  std::string fault;
  for (std::size_t i = 0; i < data.size(); ++i) {
    const unsigned value = byte_at(data, i);
    if (value == '>') {
      break;
    }
    if (is_white(value)) {
      continue;
    }
    int digit = -1;
    if (value >= '0' && value <= '9') {
      digit = static_cast<int>(value - '0');
    } else if (value >= 'a' && value <= 'f') {
      digit = static_cast<int>(value - 'a' + 10);
    } else if (value >= 'A' && value <= 'F') {
      digit = static_cast<int>(value - 'A' + 10);
    } else {
      fault = "the ASCIIHex data holds a byte that is not a hexadecimal digit at offset " +
              std::to_string(i);
      break;
    }
    if (pending < 0) {
      pending = digit;
    } else {
      out.push_back(std::byte{static_cast<unsigned char>((static_cast<unsigned>(pending) << 4U) |
                                                         static_cast<unsigned>(digit))});
      pending = -1;
    }
  }
  if (pending >= 0) {
    out.push_back(std::byte{static_cast<unsigned char>(static_cast<unsigned>(pending) << 4U)});
  }
  return finish(std::move(out), fault);
}

// ASCII85Decode, ISO 32000-1 section 7.4.3.
Stage ascii85(ByteSpan data, platform::Budget& budget, std::string_view object) {
  Buffer out(budget, std::string(object));
  std::size_t at = 0;
  if (data.size() >= 2 && byte_at(data, 0) == '<' && byte_at(data, 1) == '~') {
    at = 2;
  }
  std::array<unsigned, 5> group{};
  std::size_t count = 0;
  std::string fault;
  const auto flush = [&](std::size_t digits) {
    for (std::size_t i = digits; i < 5; ++i) {
      group.at(i) = 84;  // 'u' - '!'
    }
    std::uint64_t value = 0;
    for (const unsigned digit : group) {
      value = value * 85 + digit;
    }
    if (value > 0xFFFFFFFFULL) {
      fault = "an ASCII85 group exceeds 2^32 - 1";
      return;
    }
    for (std::size_t i = 0; i + 1 < digits; ++i) {
      out.push_back(std::byte{static_cast<unsigned char>((value >> (24U - 8U * i)) & 0xFFU)});
    }
  };
  for (; at < data.size(); ++at) {
    const unsigned value = byte_at(data, at);
    if (value == '~') {
      break;
    }
    if (is_white(value)) {
      continue;
    }
    if (value == 'z' && count == 0) {
      out.append(std::array<std::byte, 4>{});
      continue;
    }
    if (value < '!' || value > 'u') {
      fault = "the ASCII85 data holds an invalid byte at offset " + std::to_string(at);
      break;
    }
    group.at(count++) = value - '!';
    if (count == 5) {
      flush(5);
      count = 0;
      if (!fault.empty()) {
        break;
      }
    }
  }
  if (fault.empty() && count == 1) {
    fault = "the ASCII85 data ends with a single digit";
  } else if (fault.empty() && count > 1) {
    flush(count);
  }
  return finish(std::move(out), fault);
}

// RunLengthDecode, ISO 32000-1 section 7.4.5.
Stage run_length(ByteSpan data, platform::Budget& budget, std::string_view object) {
  Buffer out(budget, std::string(object));
  std::size_t at = 0;
  while (at < data.size()) {
    const unsigned length = byte_at(data, at++);
    if (length == 128) {
      return finish(std::move(out));
    }
    if (length < 128) {
      const std::size_t count = length + 1U;
      if (data.size() - at < count) {
        out.append(data.subspan(at));
        return finish(std::move(out), "the RunLength data ends inside a literal run");
      }
      out.append(data.subspan(at, count));
      at += count;
    } else {
      if (at >= data.size()) {
        return finish(std::move(out), "the RunLength data ends before a repeated byte");
      }
      const std::byte repeated = data[at++];
      for (unsigned i = 0; i < 257U - length; ++i) {
        out.push_back(repeated);
      }
    }
  }
  return finish(std::move(out));
}

struct PredictorParameters {
  std::int64_t predictor = 1;
  std::int64_t colors = 1;
  std::int64_t bits = 8;
  std::int64_t columns = 1;
};

PredictorParameters predictor_parameters(const Object& parameters) {
  PredictorParameters result;
  const Dictionary* dictionary = parameters.dictionary();
  if (dictionary == nullptr) {
    return result;
  }
  result.predictor = dictionary->get("Predictor").integer().value_or(1);
  result.colors = dictionary->get("Colors").integer().value_or(1);
  result.bits = dictionary->get("BitsPerComponent").integer().value_or(8);
  result.columns = dictionary->get("Columns").integer().value_or(1);
  return result;
}

// The TIFF predictor 2 and the PNG predictors 10 to 15 (ISO 32000-1 section
// 7.4.4.4). The PNG filter type is read from each row whatever the predictor
// number says, as every reader does.
Stage predict(std::vector<std::byte> input, const PredictorParameters& p, platform::Budget& budget,
              std::string_view object) {
  Stage stage;
  if (p.predictor <= 1) {
    stage.bytes = std::move(input);
    return stage;
  }
  const bool valid_bits = p.bits == 1 || p.bits == 2 || p.bits == 4 || p.bits == 8 || p.bits == 16;
  if (!valid_bits || p.colors < 1 || p.colors > 32 || p.columns < 1 ||
      (p.predictor != 2 && (p.predictor < 10 || p.predictor > 15))) {
    stage.bytes = std::move(input);
    stage.damaged = true;
    stage.fault = "the predictor parameters are invalid (/Predictor " +
                  std::to_string(p.predictor) + ", /Colors " + std::to_string(p.colors) +
                  ", /BitsPerComponent " + std::to_string(p.bits) + ")";
    return stage;
  }
  const std::string what = "the predictor row of " + std::string(object);
  const auto pixel_bits = static_cast<std::uint64_t>(p.colors * p.bits);
  const std::uint64_t row_bits =
      platform::checked_mul(pixel_bits, static_cast<std::uint64_t>(p.columns), what);
  const std::uint64_t row_bytes = (row_bits + 7) / 8;
  budget.check_claim(row_bytes, what);
  const auto row = static_cast<std::size_t>(row_bytes);
  const std::size_t bpp = std::max<std::size_t>(1, static_cast<std::size_t>((pixel_bits + 7) / 8));
  Buffer out(budget, std::string(object));
  std::vector<std::byte> previous(row, std::byte{0});
  std::vector<std::byte> current(row, std::byte{0});

  if (p.predictor == 2) {
    const auto colors = static_cast<std::size_t>(p.colors);
    const auto bits = static_cast<unsigned>(p.bits);
    for (std::size_t at = 0; at < input.size(); at += row) {
      const std::size_t take = std::min(row, input.size() - at);
      std::copy_n(input.begin() + static_cast<std::ptrdiff_t>(at), take, current.begin());
      if (bits == 8) {
        for (std::size_t i = colors; i < take; ++i) {
          current[i] =
              std::byte{static_cast<unsigned char>(std::to_integer<unsigned>(current[i]) +
                                                   std::to_integer<unsigned>(current[i - colors]))};
        }
      } else if (bits == 16) {
        for (std::size_t i = 2 * colors; i + 1 < take; i += 2) {
          const unsigned left = (std::to_integer<unsigned>(current[i - 2 * colors]) << 8U) |
                                std::to_integer<unsigned>(current[i - 2 * colors + 1]);
          const unsigned here = (std::to_integer<unsigned>(current[i]) << 8U) |
                                std::to_integer<unsigned>(current[i + 1]);
          const unsigned sum = (left + here) & 0xFFFFU;
          current[i] = std::byte{static_cast<unsigned char>(sum >> 8U)};
          current[i + 1] = std::byte{static_cast<unsigned char>(sum & 0xFFU)};
        }
      } else {
        const unsigned mask = (1U << bits) - 1U;
        const std::size_t samples =
            std::min<std::size_t>(static_cast<std::size_t>(p.columns) * colors, take * 8 / bits);
        const auto get = [&](std::size_t index) {
          const std::size_t bit = index * bits;
          const unsigned shift = 8U - bits - static_cast<unsigned>(bit % 8);
          return (std::to_integer<unsigned>(current[bit / 8]) >> shift) & mask;
        };
        const auto put = [&](std::size_t index, unsigned value) {
          const std::size_t bit = index * bits;
          const unsigned shift = 8U - bits - static_cast<unsigned>(bit % 8);
          const auto old = std::to_integer<unsigned>(current[bit / 8]);
          current[bit / 8] = std::byte{
              static_cast<unsigned char>((old & ~(mask << shift)) | ((value & mask) << shift))};
        };
        for (std::size_t i = colors; i < samples; ++i) {
          put(i, get(i) + get(i - colors));
        }
      }
      out.append(std::span(current.data(), take));
    }
    return finish(std::move(out));
  }

  std::size_t at = 0;
  std::string fault;
  while (at < input.size()) {
    const auto type = std::to_integer<unsigned>(input[at++]);
    const std::size_t take = std::min(row, input.size() - at);
    for (std::size_t i = 0; i < take; ++i) {
      const auto raw = std::to_integer<unsigned>(input[at + i]);
      const auto left = i >= bpp ? std::to_integer<unsigned>(current[i - bpp]) : 0U;
      const auto up = std::to_integer<unsigned>(previous[i]);
      const auto upper_left = i >= bpp ? std::to_integer<unsigned>(previous[i - bpp]) : 0U;
      unsigned value = raw;
      switch (type) {
        case 0:
          break;
        case 1:
          value = raw + left;
          break;
        case 2:
          value = raw + up;
          break;
        case 3:
          value = raw + (left + up) / 2;
          break;
        case 4: {
          const int estimate = static_cast<int>(left + up) - static_cast<int>(upper_left);
          const int pa = std::abs(estimate - static_cast<int>(left));
          const int pb = std::abs(estimate - static_cast<int>(up));
          const int pc = std::abs(estimate - static_cast<int>(upper_left));
          value = raw + ((pa <= pb && pa <= pc) ? left : (pb <= pc ? up : upper_left));
          break;
        }
        default:
          if (fault.empty()) {
            fault = "a PNG predictor row names the unknown filter type " + std::to_string(type);
          }
          break;
      }
      current[i] = std::byte{static_cast<unsigned char>(value & 0xFFU)};
    }
    out.append(std::span(current.data(), take));
    at += take;
    std::swap(previous, current);
  }
  return finish(std::move(out), fault);
}

[[nodiscard]] std::vector<std::pair<std::string, Object>> chain_of(const Object& filter,
                                                                   const Object& parameters) {
  std::vector<std::pair<std::string, Object>> chain;
  const auto parameter_at = [&](std::size_t index) {
    if (const Object::Array* list = parameters.array()) {
      return index < list->size() ? (*list)[index] : Object();
    }
    return index == 0 ? parameters : Object();
  };
  if (const std::string* name = filter.name()) {
    chain.emplace_back(*name, parameter_at(0));
  } else if (const Object::Array* names = filter.array()) {
    for (std::size_t i = 0; i < names->size(); ++i) {
      const std::string* item = (*names)[i].name();
      chain.emplace_back(item != nullptr ? *item : std::string("(not a name)"), parameter_at(i));
    }
  }
  return chain;
}

}  // namespace

std::string_view Filters::full_name(std::string_view name) noexcept {
  if (name == "AHx") {
    return "ASCIIHexDecode";
  }
  if (name == "A85") {
    return "ASCII85Decode";
  }
  if (name == "LZW") {
    return "LZWDecode";
  }
  if (name == "Fl") {
    return "FlateDecode";
  }
  if (name == "RL") {
    return "RunLengthDecode";
  }
  if (name == "CCF") {
    return "CCITTFaxDecode";
  }
  if (name == "DCT") {
    return "DCTDecode";
  }
  return name;
}

bool Filters::is_image_codec(std::string_view name) noexcept {
  const std::string_view full = full_name(name);
  return full == "DCTDecode" || full == "CCITTFaxDecode" || full == "JBIG2Decode" ||
         full == "JPXDecode";
}

Filters::Result Filters::decode(platform::ByteSpan data, const Object& filter,
                                const Object& parameters, platform::Budget& budget,
                                std::string_view object) {
  Result result;
  result.bytes.assign(data.begin(), data.end());
  const auto chain = chain_of(filter, parameters);
  for (std::size_t i = 0; i < chain.size(); ++i) {
    const std::string_view name = full_name(chain[i].first);
    const Object& parameter = chain[i].second;
    Stage stage;
    if (name == "FlateDecode") {
      stage = flate(result.bytes, budget, object);
    } else if (name == "LZWDecode") {
      const Dictionary* dictionary = parameter.dictionary();
      const std::int64_t early =
          dictionary != nullptr ? dictionary->get("EarlyChange").integer().value_or(1) : 1;
      stage = lzw(result.bytes, static_cast<int>(early != 0), budget, object);
    } else if (name == "ASCIIHexDecode") {
      stage = ascii_hex(result.bytes, budget, object);
    } else if (name == "ASCII85Decode") {
      stage = ascii85(result.bytes, budget, object);
    } else if (name == "RunLengthDecode") {
      stage = run_length(result.bytes, budget, object);
    } else if (name == "Crypt") {
      continue;
    } else {
      for (std::size_t j = i; j < chain.size(); ++j) {
        result.remaining.emplace_back(full_name(chain[j].first));
        result.remaining_parameters.push_back(chain[j].second);
      }
      break;
    }
    if (name == "FlateDecode" || name == "LZWDecode") {
      Stage predicted =
          predict(std::move(stage.bytes), predictor_parameters(parameter), budget, object);
      stage.bytes = std::move(predicted.bytes);
      if (predicted.damaged && !stage.damaged) {
        stage.damaged = true;
        stage.fault = std::move(predicted.fault);
      }
    }
    result.bytes = std::move(stage.bytes);
    if (stage.damaged && !result.damaged) {
      result.damaged = true;
      result.fault = std::string(name) + ": " + stage.fault;
    }
  }
  return result;
}

}  // namespace pdfkit::syntax
