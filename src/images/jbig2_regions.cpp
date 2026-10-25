// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The decoding procedures of JBIG2, a port of the decoding functions of pdf.js
// src/core/jbig2.js at commit fe48736846e2043696738bd54435ebb5ab24ecff (read;
// Apache-2.0) over the clause numbers of ITU-T T.88 (recalled). Where pdf.js
// stops with "is not supported" this file follows the standard instead:
// typical prediction in generic refinement (6.3.5.6), the halftone skip bitmap
// and all five combination operators, the AND and XNOR operators of text
// regions, and Huffman coding with refinement in symbol dictionaries and text
// regions (6.4.11, 6.5.8.2). Context labels are formed in the standard's bit
// order, so the typical-prediction pseudo-pixel contexts of the standard
// (T.88 6.2.5.7) are right for any placement of the adaptive template pixels.
// Every loop is bounded by the data, by a count checked against the budget
// before it starts, or by the size of a bitmap created under the budget.

#include "images/jbig2_regions.hpp"

#include "images/arithmetic_decoder.hpp"
#include "images/ccitt_decoder.hpp"
#include "images/decode_fault.hpp"
#include "platform/checked.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace pdfkit::images {

namespace {

using Allowance = Jbig2Bitmap::Allowance;
using AdaptivePixel = Jbig2Segments::AdaptivePixel;
using Bitmap = Jbig2Bitmap;
using BitmapList = Jbig2Regions::BitmapList;
using Table = Jbig2Huffman::Table;
using TableList = Jbig2Regions::TableList;
using IntegerContexts = std::array<std::uint8_t, 512>;

// The bytes a counted item is charged, so a count claimed by the file is bounded
// by the budget before anything is reserved or a loop over it starts: a symbol
// stands for a shared bitmap and its list entry, an instance, pattern or grid
// cell for the time it takes, and a symbol of a Huffman table for its codes in
// the decoding tree.
constexpr std::uint64_t k_bytes_per_symbol = 128;
constexpr std::uint64_t k_bytes_per_item = 16;
constexpr std::uint64_t k_bytes_per_coded_symbol = 512;

[[nodiscard]] unsigned ceil_log2(std::uint64_t value) {
  return value <= 1 ? 0U : static_cast<unsigned>(std::bit_width(value - 1));
}

[[nodiscard]] std::int64_t floor_half(std::int64_t value) {
  return value >= 0 ? value / 2 : -((-value + 1) / 2);
}

[[nodiscard]] std::int64_t floor_div_256(std::int64_t value) {
  return value >= 0 ? value / 256 : -((-value + 255) / 256);
}

void claim_items(const Allowance& allowance, std::uint64_t count, const char* what,
                 std::uint64_t bytes_each = k_bytes_per_item) {
  allowance.budget->check_claim(platform::checked_mul<std::uint64_t>(count, bytes_each, what),
                                std::string(what) + " of " + allowance.object);
}

Bitmap::Operator to_operator(unsigned code) {
  switch (code) {
    case 0:
      return Bitmap::Operator::bit_or;
    case 1:
      return Bitmap::Operator::bit_and;
    case 2:
      return Bitmap::Operator::bit_xor;
    case 3:
      return Bitmap::Operator::bit_xnor;
    case 4:
      return Bitmap::Operator::replace;
    default:
      throw DecodeFault("the JBIG2 combination operator " + std::to_string(code) +
                        " is not one the standard defines");
  }
}

[[nodiscard]] std::uint32_t to_size(std::int64_t value, const char* what) {
  if (value < 0 || value > std::numeric_limits<std::int32_t>::max()) {
    throw DecodeFault(std::string("the JBIG2 data gives ") + what + " the size " +
                      std::to_string(value));
  }
  return static_cast<std::uint32_t>(value);
}

// ---------------------------------------------------------------------------
// Annex A: arithmetic integer decoding, and the IAID procedure.

// The context tables one arithmetic-coded procedure keeps: one for each
// integer procedure of annex A, the generic and refinement bitmaps' contexts,
// and the symbol ID contexts.
struct Contexts {
  std::vector<std::uint8_t> generic = std::vector<std::uint8_t>(std::size_t{1} << 16U, 0);
  std::vector<std::uint8_t> refinement = std::vector<std::uint8_t>(std::size_t{1} << 16U, 0);
  IntegerContexts delta_height{};
  IntegerContexts delta_width{};
  IntegerContexts export_run{};
  IntegerContexts aggregate_instances{};
  IntegerContexts delta_t{};
  IntegerContexts first_s{};
  IntegerContexts delta_s{};
  IntegerContexts instance_t{};
  IntegerContexts refinement_flag{};
  IntegerContexts refinement_dw{};
  IntegerContexts refinement_dh{};
  IntegerContexts refinement_dx{};
  IntegerContexts refinement_dy{};
  std::vector<std::uint8_t> symbol_id;
};

// A.2: the value, or no value for the out-of-band code.
std::optional<std::int64_t> decode_integer(ArithmeticDecoder& decoder, IntegerContexts& contexts) {
  unsigned prev = 1;
  const auto read_bits = [&](unsigned length) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < length; ++i) {
      const unsigned bit = decoder.read_bit(contexts[prev]);
      prev = prev < 256 ? (prev << 1U) | bit : (((prev << 1U) | bit) & 511U) | 256U;
      value = (value << 1U) | bit;
    }
    return static_cast<std::int64_t>(value);
  };
  const std::int64_t sign = read_bits(1);
  std::int64_t value = 0;
  if (read_bits(1) != 0) {
    if (read_bits(1) != 0) {
      if (read_bits(1) != 0) {
        if (read_bits(1) != 0) {
          if (read_bits(1) != 0) {
            value = read_bits(32) + 4436;
          } else {
            value = read_bits(12) + 340;
          }
        } else {
          value = read_bits(8) + 84;
        }
      } else {
        value = read_bits(6) + 20;
      }
    } else {
      value = read_bits(4) + 4;
    }
  } else {
    value = read_bits(2);
  }
  std::int64_t signed_value = 0;
  if (sign == 0) {
    signed_value = value;
  } else if (value > 0) {
    signed_value = -value;
  } else {
    return std::nullopt;  // negative zero is the out-of-band value
  }
  if (signed_value < std::numeric_limits<std::int32_t>::min() ||
      signed_value > std::numeric_limits<std::int32_t>::max()) {
    return std::nullopt;
  }
  return signed_value;
}

// A.3
std::uint32_t decode_iaid(ArithmeticDecoder& decoder, std::vector<std::uint8_t>& contexts,
                          unsigned code_length) {
  std::uint64_t prev = 1;
  for (unsigned i = 0; i < code_length; ++i) {
    const std::uint32_t bit = decoder.read_bit(contexts.at(static_cast<std::size_t>(prev)));
    prev = (prev << 1U) | bit;
  }
  const std::uint64_t mask =
      code_length < 31 ? (std::uint64_t{1} << code_length) - 1U : 0x7fffffffU;
  return static_cast<std::uint32_t>(prev & mask);
}

// The arithmetic decoder of one segment together with its contexts.
struct Coder {
  Coder(platform::ByteSpan data, const Allowance& allowance, unsigned symbol_code_length)
      : decoder(data) {
    if (symbol_code_length > 0) {
      const std::uint64_t size = std::uint64_t{1} << std::min(symbol_code_length + 1U, 40U);
      allowance.budget->check_claim(size, "the symbol ID contexts of " + allowance.object);
      contexts.symbol_id.assign(static_cast<std::size_t>(size), 0);
    }
  }
  ArithmeticDecoder decoder;
  Contexts contexts;
};

// ---------------------------------------------------------------------------
// MMR (T.88 6.2.6) through the CCITT decoder: Group 4 coding with black as 1.

struct MmrResult {
  Bitmap bitmap;
  std::size_t consumed = 0;
};

MmrResult decode_mmr(platform::ByteSpan data, std::uint32_t width, std::uint32_t height,
                     bool end_of_block, Allowance& allowance) {
  Bitmap bitmap = Bitmap::create(allowance, width, height, 0, "an MMR bitmap");
  MmrResult result;
  if (width == 0 || height == 0) {
    result.bitmap = std::move(bitmap);
    return result;
  }
  CcittDecoder::Parameters parameters;
  parameters.k = -1;
  parameters.columns = width;
  parameters.rows = height;
  parameters.black_is_1 = true;
  parameters.end_of_block = end_of_block;
  const CcittDecoder::Result decoded =
      CcittDecoder::decode(data, parameters, *allowance.budget, allowance.object);
  const std::size_t row_bytes = (static_cast<std::size_t>(width) + 7U) / 8U;
  // A fault or fewer rows than the region holds leaves the rest of the bitmap
  // white, as pdf.js reads it.
  for (std::uint32_t y = 0; y < decoded.rows && y < height; ++y) {
    auto row = bitmap.row(y);
    for (std::uint32_t x = 0; x < width; ++x) {
      const auto byte = std::to_integer<unsigned>(decoded.data[y * row_bytes + x / 8U]);
      row[x] = static_cast<std::uint8_t>((byte >> (7U - x % 8U)) & 1U);
    }
  }
  result.bitmap = std::move(bitmap);
  result.consumed = decoded.bytes_consumed;
  return result;
}

// ---------------------------------------------------------------------------
// 6.2: generic region decoding.

struct Offset {
  int dx;
  int dy;
};

// A context template: the pixels in label order from the most significant bit,
// with at >= 0 naming the adaptive pixel that stands in the slot.
struct Slot {
  int dx;
  int dy;
  int at;
};

constexpr std::array<Slot, 16> k_template_0 = {{
    {-2, -2, 3},
    {-1, -2, -1},
    {0, -2, -1},
    {1, -2, -1},
    {2, -2, 2},
    {-3, -1, 1},
    {-2, -1, -1},
    {-1, -1, -1},
    {0, -1, -1},
    {1, -1, -1},
    {2, -1, -1},
    {3, -1, 0},
    {-4, 0, -1},
    {-3, 0, -1},
    {-2, 0, -1},
    {-1, 0, -1},
}};
constexpr std::array<Slot, 13> k_template_1 = {{
    {-1, -2, -1},
    {0, -2, -1},
    {1, -2, -1},
    {2, -2, -1},
    {-2, -1, -1},
    {-1, -1, -1},
    {0, -1, -1},
    {1, -1, -1},
    {2, -1, -1},
    {3, -1, 0},
    {-3, 0, -1},
    {-2, 0, -1},
    {-1, 0, -1},
}};
constexpr std::array<Slot, 10> k_template_2 = {{
    {-1, -2, -1},
    {0, -2, -1},
    {1, -2, -1},
    {-2, -1, -1},
    {-1, -1, -1},
    {0, -1, -1},
    {1, -1, -1},
    {2, -1, 0},
    {-2, 0, -1},
    {-1, 0, -1},
}};
constexpr std::array<Slot, 10> k_template_3 = {{
    {-3, -1, -1},
    {-2, -1, -1},
    {-1, -1, -1},
    {0, -1, -1},
    {1, -1, -1},
    {2, -1, 0},
    {-4, 0, -1},
    {-3, 0, -1},
    {-2, 0, -1},
    {-1, 0, -1},
}};

// 6.2.5.7: the context a typical-prediction decision is coded in.
constexpr std::array<std::uint32_t, 4> k_typical_contexts = {0x9b25, 0x0795, 0x00e5, 0x0195};

// The adaptive pixels the standard gives each template unless the segment moves
// them.
constexpr std::array<std::array<Offset, 4>, 4> k_default_at = {{
    {{{3, -1}, {-3, -1}, {2, -2}, {-2, -2}}},
    {{{3, -1}, {0, 0}, {0, 0}, {0, 0}}},
    {{{2, -1}, {0, 0}, {0, 0}, {0, 0}}},
    {{{2, -1}, {0, 0}, {0, 0}, {0, 0}}},
}};

// A run of consecutive pixels of a template within one row: label bits hi down
// to hi - length + 1, from dx = first.
struct Run {
  int dy;
  int first;
  int length;
  int hi;
};

constexpr std::array<std::array<Run, 3>, 4> k_runs = {{
    {{{-2, -2, 5, 15}, {-1, -3, 7, 10}, {0, -4, 4, 3}}},
    {{{-2, -1, 4, 12}, {-1, -2, 6, 8}, {0, -3, 3, 2}}},
    {{{-2, -1, 3, 9}, {-1, -2, 5, 6}, {0, -2, 2, 1}}},
    {{{-1, -3, 6, 9}, {0, -4, 4, 3}, {0, 0, 0, 0}}},
}};

[[nodiscard]] std::vector<Offset> template_offsets(unsigned template_index,
                                                   const std::vector<AdaptivePixel>& at) {
  std::vector<Slot> slots;
  switch (template_index) {
    case 0:
      slots.assign(k_template_0.begin(), k_template_0.end());
      break;
    case 1:
      slots.assign(k_template_1.begin(), k_template_1.end());
      break;
    case 2:
      slots.assign(k_template_2.begin(), k_template_2.end());
      break;
    default:
      slots.assign(k_template_3.begin(), k_template_3.end());
      break;
  }
  std::vector<Offset> offsets;
  for (const Slot& slot : slots) {
    if (slot.at < 0) {
      offsets.push_back({slot.dx, slot.dy});
    } else {
      if (static_cast<std::size_t>(slot.at) >= at.size()) {
        throw DecodeFault("a JBIG2 generic region lacks an adaptive template pixel");
      }
      offsets.push_back(
          {at[static_cast<std::size_t>(slot.at)].x, at[static_cast<std::size_t>(slot.at)].y});
    }
  }
  return offsets;
}

[[nodiscard]] bool has_default_at(unsigned template_index, const std::vector<AdaptivePixel>& at) {
  const unsigned wanted = template_index == 0 ? 4U : 1U;
  if (at.size() < wanted) {
    return false;
  }
  for (unsigned i = 0; i < wanted; ++i) {
    if (at[i].x != k_default_at[template_index][i].dx ||
        at[i].y != k_default_at[template_index][i].dy) {
      return false;
    }
  }
  return true;
}

// Decodes the pixels of bitmap (which holds zeros) with the arithmetic decoder
// under the generic region procedure: skip names pixels set to 0 without
// decoding (halftone regions), prediction turns on typical prediction.
void decode_generic(Bitmap& bitmap, ArithmeticDecoder& decoder, std::vector<std::uint8_t>& contexts,
                    unsigned template_index, bool prediction, const Bitmap* skip,
                    const std::vector<AdaptivePixel>& at) {
  const std::uint32_t width = bitmap.width();
  const std::uint32_t height = bitmap.height();
  const std::vector<Offset> offsets = template_offsets(template_index, at);
  const bool fast = has_default_at(template_index, at);
  const auto& runs = k_runs[template_index];
  std::uint32_t keep = 0;
  for (const Run& run : runs) {
    if (run.length > 1) {
      keep |= ((1U << static_cast<unsigned>(run.length - 1)) - 1U)
              << static_cast<unsigned>(run.hi - run.length + 2);
    }
  }
  const std::uint32_t typical = k_typical_contexts[template_index];
  unsigned ltp = 0;
  for (std::uint32_t y = 0; y < height; ++y) {
    if (prediction) {
      ltp ^= decoder.read_bit(contexts[typical]);
      if (ltp != 0) {
        if (y > 0) {
          const auto from = bitmap.row(y - 1);
          std::ranges::copy(from, bitmap.row(y).begin());
        }
        continue;
      }
    }
    auto row = bitmap.row(y);
    std::uint32_t label = 0;
    if (fast) {
      for (const Run& run : runs) {
        for (int t = 0; t < run.length; ++t) {
          if (bitmap.get(run.first + t, static_cast<std::int64_t>(y) + run.dy) != 0) {
            label |= 1U << static_cast<unsigned>(run.hi - t);
          }
        }
      }
    }
    for (std::uint32_t x = 0; x < width; ++x) {
      if (!fast) {
        label = 0;
        for (const Offset& offset : offsets) {
          label = (label << 1U) | bitmap.get(static_cast<std::int64_t>(x) + offset.dx,
                                             static_cast<std::int64_t>(y) + offset.dy);
        }
      }
      std::uint8_t pixel = 0;
      if (skip == nullptr || skip->get(x, y) == 0) {
        pixel = static_cast<std::uint8_t>(decoder.read_bit(contexts[label]));
      }
      row[x] = pixel;
      if (fast) {
        std::uint32_t next = (label << 1U) & keep;
        for (const Run& run : runs) {
          if (run.length == 0) {
            continue;
          }
          const auto low = static_cast<unsigned>(run.hi - run.length + 1);
          if (run.dy == 0) {
            next |= pixel;
          } else {
            next |= static_cast<std::uint32_t>(
                        bitmap.get(static_cast<std::int64_t>(x) + 1 + run.first + run.length - 1,
                                   static_cast<std::int64_t>(y) + run.dy))
                    << low;
          }
        }
        label = next;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// 6.3: generic refinement region decoding.

struct RefinementTemplate {
  std::vector<Offset> coding;
  std::vector<Offset> reference;
};

RefinementTemplate refinement_template(unsigned template_index,
                                       const std::vector<AdaptivePixel>& at) {
  RefinementTemplate shape;
  if (template_index == 0) {
    if (at.size() < 2) {
      throw DecodeFault("a JBIG2 refinement region lacks its adaptive template pixels");
    }
    shape.coding = {{0, -1}, {1, -1}, {-1, 0}, {at[0].x, at[0].y}};
    shape.reference = {{0, -1}, {1, -1}, {-1, 0},           {0, 0}, {1, 0}, {-1, 1},
                       {0, 1},  {1, 1},  {at[1].x, at[1].y}};
  } else {
    shape.coding = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}};
    shape.reference = {{0, -1}, {-1, 0}, {0, 0}, {1, 0}, {0, 1}, {1, 1}};
  }
  return shape;
}

// The refinement of reference, offset by (offset_x, offset_y), into bitmap.
void decode_refinement(Bitmap& bitmap, unsigned template_index, const Bitmap& reference,
                       std::int64_t offset_x, std::int64_t offset_y, bool prediction,
                       const std::vector<AdaptivePixel>& at, ArithmeticDecoder& decoder,
                       std::vector<std::uint8_t>& contexts) {
  const RefinementTemplate shape = refinement_template(template_index, at);
  const std::uint32_t typical = template_index == 0 ? 0x0020U : 0x0008U;
  unsigned ltp = 0;
  for (std::uint32_t y = 0; y < bitmap.height(); ++y) {
    if (prediction) {
      ltp ^= decoder.read_bit(contexts[typical]);
    }
    auto row = bitmap.row(y);
    for (std::uint32_t x = 0; x < bitmap.width(); ++x) {
      const std::int64_t rx = static_cast<std::int64_t>(x) - offset_x;
      const std::int64_t ry = static_cast<std::int64_t>(y) - offset_y;
      if (ltp != 0) {
        // 6.3.5.6: a pixel whose 3 by 3 reference neighbourhood is of one value
        // takes that value without a decoded decision.
        const std::uint8_t centre = reference.get(rx, ry);
        bool uniform = true;
        for (int dy = -1; dy <= 1 && uniform; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            if (reference.get(rx + dx, ry + dy) != centre) {
              uniform = false;
              break;
            }
          }
        }
        if (uniform) {
          row[x] = centre;
          continue;
        }
      }
      std::uint32_t label = 0;
      for (const Offset& offset : shape.coding) {
        label = (label << 1U) | bitmap.get(static_cast<std::int64_t>(x) + offset.dx,
                                           static_cast<std::int64_t>(y) + offset.dy);
      }
      for (const Offset& offset : shape.reference) {
        label = (label << 1U) | reference.get(rx + offset.dx, ry + offset.dy);
      }
      row[x] = static_cast<std::uint8_t>(decoder.read_bit(contexts[label]));
    }
  }
}

// ---------------------------------------------------------------------------
// 6.4: text region decoding.

// The Huffman tables a text region reads from, and the symbol code source.
struct TextTables {
  const Table* symbol_ids = nullptr;
  const Table* first_s = nullptr;
  const Table* delta_s = nullptr;
  const Table* delta_t = nullptr;
  const Table* refinement_dw = nullptr;
  const Table* refinement_dh = nullptr;
  const Table* refinement_dx = nullptr;
  const Table* refinement_dy = nullptr;
  const Table* refinement_size = nullptr;
};

struct TextParameters {
  bool huffman = false;
  bool refinement = false;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  unsigned default_pixel = 0;
  std::uint64_t instances = 0;
  unsigned strip_size = 1;
  unsigned log_strip_size = 0;
  unsigned symbol_code_length = 0;
  bool transposed = false;
  int ds_offset = 0;
  unsigned reference_corner = 0;
  unsigned combination_operator = 0;
  unsigned refinement_template = 0;
  std::vector<AdaptivePixel> refinement_at;
};

std::int64_t require(const std::optional<std::int64_t>& value) {
  return value.value_or(0);
}

// Two runs of symbols seen as one list: the symbols a dictionary was given and
// those it has decoded so far.
struct SymbolView {
  const BitmapList* first;
  const BitmapList* second;

  [[nodiscard]] std::size_t size() const noexcept { return first->size() + second->size(); }
  [[nodiscard]] const std::shared_ptr<const Bitmap>& at(std::size_t index) const {
    return index < first->size() ? (*first)[index] : (*second)[index - first->size()];
  }
};

// Decodes one text region into a new bitmap. coder carries the arithmetic
// decoder and contexts (unused for symbol IDs when huffman is set); reader the
// Huffman-coded bits.
Bitmap decode_text_region(const TextParameters& p, const SymbolView& symbols,
                          const TextTables& tables, Coder* coder, Jbig2Huffman::Reader* reader,
                          Allowance& allowance) {
  Bitmap bitmap =
      Bitmap::create(allowance, p.width, p.height, static_cast<std::uint8_t>(p.default_pixel),
                     "a text region bitmap");
  claim_items(allowance, p.instances, "the symbol instances of a text region");

  const auto decode_delta = [&](const Table* table, IntegerContexts* contexts) {
    return p.huffman ? table->decode(*reader) : decode_integer(coder->decoder, *contexts);
  };
  Contexts* contexts = coder != nullptr ? &coder->contexts : nullptr;
  const auto delta_t = [&] {
    return decode_delta(tables.delta_t, contexts ? &contexts->delta_t : nullptr);
  };

  std::int64_t strip_t = -require(delta_t());
  std::int64_t first_s = 0;
  std::uint64_t decoded = 0;
  while (decoded < p.instances) {
    strip_t += require(delta_t());
    first_s += require(decode_delta(tables.first_s, contexts ? &contexts->first_s : nullptr));
    std::int64_t current_s = first_s;
    while (true) {
      std::int64_t current_t = 0;
      if (p.strip_size > 1) {
        current_t = p.huffman ? reader->read_bits(p.log_strip_size)
                              : require(decode_integer(coder->decoder, contexts->instance_t));
      }
      const std::int64_t t = static_cast<std::int64_t>(p.strip_size) * strip_t + current_t;
      std::uint64_t symbol_id = 0;
      if (p.huffman) {
        symbol_id = tables.symbol_ids != nullptr
                        ? static_cast<std::uint64_t>(require(tables.symbol_ids->decode(*reader)))
                        : reader->read_bits(p.symbol_code_length);
      } else {
        symbol_id = decode_iaid(coder->decoder, contexts->symbol_id, p.symbol_code_length);
      }
      if (symbol_id >= symbols.size()) {
        throw DecodeFault("a JBIG2 text region names the symbol " + std::to_string(symbol_id) +
                          " of only " + std::to_string(symbols.size()));
      }
      std::shared_ptr<const Bitmap> symbol = symbols.at(static_cast<std::size_t>(symbol_id));
      std::int64_t symbol_width = symbol->width();
      std::int64_t symbol_height = symbol->height();
      bool apply_refinement = false;
      if (p.refinement) {
        apply_refinement =
            p.huffman ? reader->read_bit() != 0
                      : require(decode_integer(coder->decoder, contexts->refinement_flag)) != 0;
      }
      if (apply_refinement) {
        std::int64_t rdw = 0;
        std::int64_t rdh = 0;
        std::int64_t rdx = 0;
        std::int64_t rdy = 0;
        if (p.huffman) {
          rdw = require(tables.refinement_dw->decode(*reader));
          rdh = require(tables.refinement_dh->decode(*reader));
          rdx = require(tables.refinement_dx->decode(*reader));
          rdy = require(tables.refinement_dy->decode(*reader));
        } else {
          rdw = require(decode_integer(coder->decoder, contexts->refinement_dw));
          rdh = require(decode_integer(coder->decoder, contexts->refinement_dh));
          rdx = require(decode_integer(coder->decoder, contexts->refinement_dx));
          rdy = require(decode_integer(coder->decoder, contexts->refinement_dy));
        }
        symbol_width += rdw;
        symbol_height += rdh;
        Bitmap refined = Bitmap::create(allowance, to_size(symbol_width, "a refined symbol"),
                                        to_size(symbol_height, "a refined symbol"), 0,
                                        "a refined symbol bitmap");
        const std::int64_t offset_x = floor_half(rdw) + rdx;
        const std::int64_t offset_y = floor_half(rdh) + rdy;
        if (p.huffman) {
          // 6.4.11: the refinement bitmap is arithmetic-coded in a byte-aligned
          // run whose length the stream states, with fresh decoder state and
          // shared contexts.
          const std::int64_t size = require(tables.refinement_size->decode(*reader));
          reader->byte_align();
          const std::size_t start = reader->position();
          const platform::ByteSpan data = reader->data();
          if (size < 0 || start > data.size() ||
              static_cast<std::uint64_t>(size) > data.size() - start) {
            throw DecodeFault("a JBIG2 refined symbol's data runs past the segment");
          }
          ArithmeticDecoder decoder(data.subspan(start, static_cast<std::size_t>(size)));
          decode_refinement(refined, p.refinement_template, *symbol, offset_x, offset_y, false,
                            p.refinement_at, decoder, contexts->refinement);
          reader->set_position(start + static_cast<std::size_t>(size));
        } else {
          decode_refinement(refined, p.refinement_template, *symbol, offset_x, offset_y, false,
                            p.refinement_at, coder->decoder, contexts->refinement);
        }
        symbol = std::make_shared<const Bitmap>(std::move(refined));
      }

      // 6.4.5 step 3c: CURS moves to the symbol's far edge first when the
      // reference corner is on it, the symbol is placed with its reference
      // corner at (S, T) (at (T, S) when transposed), and CURS moves past the
      // symbol afterwards otherwise. The reference corner is 0 bottom left, 1
      // top left, 2 bottom right, 3 top right.
      const bool right_corner = p.reference_corner >= 2;
      const bool bottom_corner = (p.reference_corner & 1U) == 0;
      std::int64_t increment = 0;
      if (!p.transposed) {
        if (right_corner) {
          current_s += symbol_width - 1;
        } else {
          increment = symbol_width - 1;
        }
      } else if (bottom_corner) {
        current_s += symbol_height - 1;
      } else {
        increment = symbol_height - 1;
      }
      const std::int64_t corner_x = p.transposed ? t : current_s;
      const std::int64_t corner_y = p.transposed ? current_s : t;
      const std::int64_t left = corner_x - (right_corner ? symbol_width - 1 : 0);
      const std::int64_t top = corner_y - (bottom_corner ? symbol_height - 1 : 0);
      bitmap.combine(*symbol, left, top, to_operator(p.combination_operator), allowance);
      ++decoded;
      const auto delta_s = decode_delta(tables.delta_s, contexts ? &contexts->delta_s : nullptr);
      if (!delta_s || decoded >= p.instances) {
        break;
      }
      current_s += increment + *delta_s + p.ds_offset;
    }
  }
  return bitmap;
}

// ---------------------------------------------------------------------------
// Huffman table selection.

const Table* pick_table(unsigned selector, std::initializer_list<int> standard,
                        const TableList& custom, std::size_t& custom_index, const char* what) {
  const auto count = static_cast<unsigned>(standard.size());
  if (selector < count) {
    return &Jbig2Huffman::standard_table(*(standard.begin() + selector));
  }
  if (selector == 3) {
    if (custom_index >= custom.size()) {
      throw DecodeFault(std::string("a JBIG2 segment selects a custom Huffman table for ") + what +
                        " but refers to too few Tables segments");
    }
    return custom[custom_index++];
  }
  throw DecodeFault(std::string("a JBIG2 segment holds the invalid Huffman selector ") +
                    std::to_string(selector) + " for " + what);
}

}  // namespace

// ---------------------------------------------------------------------------

Bitmap Jbig2Regions::generic_region(const Jbig2Segments::GenericRegion& region,
                                    Allowance& allowance) {
  if (region.mmr) {
    return decode_mmr(region.coded, region.info.width, region.info.height, false, allowance).bitmap;
  }
  Bitmap bitmap = Bitmap::create(allowance, region.info.width, region.info.height, 0,
                                 "a generic region bitmap");
  Coder coder(region.coded, allowance, 0);
  decode_generic(bitmap, coder.decoder, coder.contexts.generic, region.template_index,
                 region.prediction, nullptr, region.at);
  return bitmap;
}

Bitmap Jbig2Regions::refinement_region(const Jbig2Segments::RefinementRegion& region,
                                       const Bitmap& reference, Allowance& allowance) {
  Bitmap bitmap = Bitmap::create(allowance, region.info.width, region.info.height, 0,
                                 "a refinement region bitmap");
  Coder coder(region.coded, allowance, 0);
  decode_refinement(bitmap, region.template_index, reference, 0, 0, region.prediction, region.at,
                    coder.decoder, coder.contexts.refinement);
  return bitmap;
}

Jbig2Regions::Dictionary Jbig2Regions::symbol_dictionary(
    const Jbig2Segments::SymbolDictionary& dictionary, const BitmapList& input_symbols,
    const TableList& custom_tables, const std::shared_ptr<const RetainedContexts>& inherited,
    Allowance& allowance) {
  const bool huffman = dictionary.huffman;
  const bool refinement = dictionary.refinement;
  const std::uint64_t new_total = dictionary.new_symbols;
  claim_items(allowance, new_total, "the new symbols of a symbol dictionary", k_bytes_per_symbol);
  claim_items(allowance, input_symbols.size() + new_total, "the symbols of a symbol dictionary",
              k_bytes_per_symbol);

  BitmapList new_symbols;
  unsigned symbol_code_length = ceil_log2(input_symbols.size() + new_total);
  const Table* delta_height = nullptr;
  const Table* delta_width = nullptr;
  const Table* bitmap_size = nullptr;
  const Table* aggregate_instances = nullptr;
  std::optional<Jbig2Huffman::Reader> reader;
  std::vector<std::uint32_t> symbol_widths;
  if (huffman) {
    std::size_t custom_index = 0;
    delta_height =
        pick_table(dictionary.dh_selector, {4, 5}, custom_tables, custom_index, "delta height");
    delta_width =
        pick_table(dictionary.dw_selector, {2, 3}, custom_tables, custom_index, "delta width");
    bitmap_size = dictionary.bitmap_size_selector != 0
                      ? pick_table(3, {}, custom_tables, custom_index, "bitmap size")
                      : &Jbig2Huffman::standard_table(1);
    aggregate_instances =
        dictionary.aggregation_selector != 0
            ? pick_table(3, {}, custom_tables, custom_index, "aggregate instances")
            : &Jbig2Huffman::standard_table(1);
    reader.emplace(dictionary.coded);
    symbol_code_length = std::max(symbol_code_length, 1U);  // 6.5.8.2.3
  }
  Coder coder(huffman && !refinement ? platform::ByteSpan() : dictionary.coded, allowance,
              refinement && !huffman ? symbol_code_length : 0);
  Contexts& contexts = coder.contexts;
  if (dictionary.context_used && !huffman && inherited) {
    contexts.generic = inherited->generic;
    contexts.refinement = inherited->refinement;
  }

  // The tables a text region nested in an aggregate symbol reads from (6.5.8.2,
  // table 17): the standard tables B.6, B.8, B.11, B.15 and B.1.
  TextTables aggregate_tables;
  if (huffman && refinement) {
    aggregate_tables.first_s = &Jbig2Huffman::standard_table(6);
    aggregate_tables.delta_s = &Jbig2Huffman::standard_table(8);
    aggregate_tables.delta_t = &Jbig2Huffman::standard_table(11);
    aggregate_tables.refinement_dw = &Jbig2Huffman::standard_table(15);
    aggregate_tables.refinement_dh = &Jbig2Huffman::standard_table(15);
    aggregate_tables.refinement_dx = &Jbig2Huffman::standard_table(15);
    aggregate_tables.refinement_dy = &Jbig2Huffman::standard_table(15);
    aggregate_tables.refinement_size = &Jbig2Huffman::standard_table(1);
  }

  const SymbolView known{&input_symbols, &new_symbols};

  std::int64_t current_height = 0;
  std::uint64_t height_classes = 0;
  while (new_symbols.size() < new_total) {
    if (++height_classes > 2 * new_total + 64) {
      throw DecodeFault("a JBIG2 symbol dictionary holds more height classes than symbols");
    }
    const auto delta_h = huffman ? delta_height->decode(*reader)
                                 : decode_integer(coder.decoder, contexts.delta_height);
    current_height += require(delta_h);
    std::int64_t current_width = 0;
    std::int64_t total_width = 0;
    const std::size_t first_symbol = symbol_widths.size();
    while (true) {
      const auto delta_w = huffman ? delta_width->decode(*reader)
                                   : decode_integer(coder.decoder, contexts.delta_width);
      if (!delta_w) {
        break;  // out-of-band: the height class is complete
      }
      if (new_symbols.size() + (huffman && !refinement ? symbol_widths.size() - first_symbol : 0) >=
          new_total) {
        throw DecodeFault("a JBIG2 symbol dictionary decodes more symbols than it declares");
      }
      current_width += *delta_w;
      total_width += current_width;
      const std::uint32_t width = to_size(current_width, "a symbol");
      const std::uint32_t height = to_size(current_height, "a symbol");
      if (refinement) {
        // 6.5.8.2: a refinement or aggregate-coded symbol.
        const std::int64_t instances =
            huffman ? require(aggregate_instances->decode(*reader))
                    : require(decode_integer(coder.decoder, contexts.aggregate_instances));
        if (instances > 1) {
          TextParameters text;
          text.huffman = huffman;
          text.refinement = true;
          text.width = width;
          text.height = height;
          text.instances = static_cast<std::uint64_t>(instances);
          text.strip_size = 1;
          text.symbol_code_length = symbol_code_length;
          text.reference_corner = 1;
          text.refinement_template = dictionary.refinement_template;
          text.refinement_at = dictionary.refinement_at;
          new_symbols.push_back(std::make_shared<const Bitmap>(decode_text_region(
              text, known, aggregate_tables, &coder, huffman ? &*reader : nullptr, allowance)));
        } else {
          std::uint64_t symbol_id = 0;
          std::int64_t rdx = 0;
          std::int64_t rdy = 0;
          if (huffman) {
            symbol_id = reader->read_bits(symbol_code_length);
            rdx = require(Jbig2Huffman::standard_table(15).decode(*reader));
            rdy = require(Jbig2Huffman::standard_table(15).decode(*reader));
          } else {
            symbol_id = decode_iaid(coder.decoder, contexts.symbol_id, symbol_code_length);
            rdx = require(decode_integer(coder.decoder, contexts.refinement_dx));
            rdy = require(decode_integer(coder.decoder, contexts.refinement_dy));
          }
          if (symbol_id >= known.size()) {
            throw DecodeFault("a JBIG2 symbol refines the symbol " + std::to_string(symbol_id) +
                              " of only " + std::to_string(known.size()));
          }
          Bitmap refined = Bitmap::create(allowance, width, height, 0, "a refined symbol bitmap");
          if (huffman) {
            const std::int64_t size = require(Jbig2Huffman::standard_table(1).decode(*reader));
            reader->byte_align();
            const std::size_t start = reader->position();
            const platform::ByteSpan data = reader->data();
            if (size < 0 || start > data.size() ||
                static_cast<std::uint64_t>(size) > data.size() - start) {
              throw DecodeFault("a JBIG2 refined symbol's data runs past the segment");
            }
            ArithmeticDecoder decoder(data.subspan(start, static_cast<std::size_t>(size)));
            decode_refinement(refined, dictionary.refinement_template,
                              *known.at(static_cast<std::size_t>(symbol_id)), rdx, rdy, false,
                              dictionary.refinement_at, decoder, contexts.refinement);
            reader->set_position(start + static_cast<std::size_t>(size));
          } else {
            decode_refinement(refined, dictionary.refinement_template,
                              *known.at(static_cast<std::size_t>(symbol_id)), rdx, rdy, false,
                              dictionary.refinement_at, coder.decoder, contexts.refinement);
          }
          new_symbols.push_back(std::make_shared<const Bitmap>(std::move(refined)));
        }
      } else if (huffman) {
        // The widths of a height class; its bitmaps are one collective bitmap.
        symbol_widths.push_back(width);
      } else {
        // 6.5.8.1: a directly coded symbol.
        Bitmap symbol = Bitmap::create(allowance, width, height, 0, "a symbol bitmap");
        decode_generic(symbol, coder.decoder, contexts.generic, dictionary.template_index, false,
                       nullptr, dictionary.at);
        new_symbols.push_back(std::make_shared<const Bitmap>(std::move(symbol)));
      }
    }
    if (huffman && !refinement) {
      // 6.5.9: the collective bitmap of the height class.
      const std::int64_t size = require(bitmap_size->decode(*reader));
      reader->byte_align();
      const std::uint32_t collective_width = to_size(total_width, "a collective bitmap");
      const std::uint32_t collective_height = to_size(current_height, "a collective bitmap");
      Bitmap collective;
      if (size == 0) {
        collective = Bitmap::create(allowance, collective_width, collective_height, 0,
                                    "a collective bitmap");
        for (std::uint32_t y = 0; y < collective_height; ++y) {
          auto row = collective.row(y);
          for (std::uint32_t x = 0; x < collective_width; ++x) {
            row[x] = static_cast<std::uint8_t>(reader->read_bit());
          }
          reader->byte_align();
        }
      } else {
        const std::size_t start = reader->position();
        const platform::ByteSpan data = reader->data();
        if (size < 0 || start > data.size() ||
            static_cast<std::uint64_t>(size) > data.size() - start) {
          throw DecodeFault("a JBIG2 collective bitmap runs past the segment");
        }
        collective = decode_mmr(data.subspan(start, static_cast<std::size_t>(size)),
                                collective_width, collective_height, false, allowance)
                         .bitmap;
        reader->set_position(start + static_cast<std::size_t>(size));
      }
      const std::size_t decoded = symbol_widths.size();
      if (first_symbol + 1 == decoded) {
        new_symbols.push_back(std::make_shared<const Bitmap>(std::move(collective)));
      } else {
        std::uint32_t x_min = 0;
        for (std::size_t i = first_symbol; i < decoded; ++i) {
          new_symbols.push_back(std::make_shared<const Bitmap>(
              collective.columns(allowance, x_min, symbol_widths[i])));
          x_min += symbol_widths[i];
        }
      }
    }
  }

  // 6.5.10: the exported symbols, selected by run lengths of alternating flags.
  const std::uint64_t total_symbols = input_symbols.size() + new_total;
  std::vector<bool> flags;
  flags.reserve(static_cast<std::size_t>(total_symbols));
  bool current_flag = false;
  std::uint64_t runs = 0;
  while (flags.size() < total_symbols) {
    if (++runs > 2 * total_symbols + 16) {
      throw DecodeFault("a JBIG2 symbol dictionary's export flags never cover its symbols");
    }
    const std::int64_t run = huffman ? require(Jbig2Huffman::standard_table(1).decode(*reader))
                                     : require(decode_integer(coder.decoder, contexts.export_run));
    if (run < 0 || static_cast<std::uint64_t>(run) > total_symbols - flags.size()) {
      throw DecodeFault("a JBIG2 symbol dictionary's export flags run past its symbols");
    }
    flags.insert(flags.end(), static_cast<std::size_t>(run), current_flag);
    current_flag = !current_flag;
  }
  BitmapList exported;
  for (std::size_t i = 0; i < input_symbols.size(); ++i) {
    if (flags[i]) {
      exported.push_back(input_symbols[i]);
    }
  }
  for (std::size_t j = 0; j < new_symbols.size(); ++j) {
    if (flags[input_symbols.size() + j]) {
      exported.push_back(new_symbols[j]);
    }
  }
  Dictionary result;
  result.symbols = std::move(exported);
  if (dictionary.context_retained && !huffman) {
    result.retained = std::make_shared<const RetainedContexts>(
        RetainedContexts{contexts.generic, contexts.refinement});
  }
  return result;
}

Bitmap Jbig2Regions::text_region(const Jbig2Segments::TextRegion& region, const BitmapList& symbols,
                                 const TableList& custom_tables, Allowance& allowance) {
  TextParameters text;
  text.huffman = region.huffman;
  text.refinement = region.refinement;
  text.width = region.info.width;
  text.height = region.info.height;
  text.default_pixel = region.default_pixel;
  text.instances = region.symbol_instances;
  text.strip_size = region.strip_size;
  text.log_strip_size = region.log_strip_size;
  text.symbol_code_length = ceil_log2(symbols.size());
  text.transposed = region.transposed;
  text.ds_offset = region.ds_offset;
  text.reference_corner = region.reference_corner;
  text.combination_operator = region.combination_operator;
  text.refinement_template = region.refinement_template;
  text.refinement_at = region.refinement_at;

  TextTables tables;
  std::optional<Jbig2Huffman::Reader> reader;
  std::optional<Table> symbol_id_table;
  if (region.huffman) {
    reader.emplace(region.coded);
    claim_items(allowance, symbols.size(), "the symbol ID table of a text region",
                k_bytes_per_coded_symbol);
    symbol_id_table = Jbig2Huffman::read_symbol_id_table(*reader, symbols.size());
    tables.symbol_ids = &*symbol_id_table;
    std::size_t custom_index = 0;
    tables.first_s = pick_table(region.huffman_fs, {6, 7}, custom_tables, custom_index, "first S");
    // Selector 2 of the first-S table is undefined; pick_table refuses it by
    // naming it invalid, and selector 3 takes a custom table.
    tables.delta_s =
        pick_table(region.huffman_ds, {8, 9, 10}, custom_tables, custom_index, "delta S");
    tables.delta_t =
        pick_table(region.huffman_dt, {11, 12, 13}, custom_tables, custom_index, "delta T");
    if (region.refinement) {
      tables.refinement_dw =
          pick_table(region.huffman_rdw, {14, 15}, custom_tables, custom_index, "refinement width");
      tables.refinement_dh = pick_table(region.huffman_rdh, {14, 15}, custom_tables, custom_index,
                                        "refinement height");
      tables.refinement_dx =
          pick_table(region.huffman_rdx, {14, 15}, custom_tables, custom_index, "refinement x");
      tables.refinement_dy =
          pick_table(region.huffman_rdy, {14, 15}, custom_tables, custom_index, "refinement y");
      tables.refinement_size =
          region.huffman_rsize != 0
              ? pick_table(3, {}, custom_tables, custom_index, "refinement size")
              : &Jbig2Huffman::standard_table(1);
    }
  }
  const bool needs_coder = !region.huffman || region.refinement;
  std::optional<Coder> coder;
  if (needs_coder) {
    coder.emplace(region.huffman ? platform::ByteSpan() : region.coded, allowance,
                  region.huffman ? 0U : text.symbol_code_length);
  }
  static const BitmapList none;
  return decode_text_region(text, SymbolView{&symbols, &none}, tables, coder ? &*coder : nullptr,
                            reader ? &*reader : nullptr, allowance);
}

BitmapList Jbig2Regions::pattern_dictionary(const Jbig2Segments::PatternDictionary& dictionary,
                                            Allowance& allowance) {
  const std::uint64_t count = static_cast<std::uint64_t>(dictionary.max_pattern_index) + 1U;
  claim_items(allowance, count, "the patterns of a pattern dictionary");
  const auto collective_width =
      platform::checked_mul<std::uint64_t>(count, dictionary.pattern_width, "a pattern dictionary");
  if (collective_width > std::numeric_limits<std::uint32_t>::max()) {
    throw DecodeFault("a JBIG2 pattern dictionary is wider than 4 billion pixels");
  }
  const auto width = static_cast<std::uint32_t>(collective_width);
  Bitmap collective;
  if (dictionary.mmr) {
    collective =
        decode_mmr(dictionary.coded, width, dictionary.pattern_height, false, allowance).bitmap;
  } else {
    std::vector<AdaptivePixel> at = {{-static_cast<int>(dictionary.pattern_width), 0}};
    if (dictionary.template_index == 0) {
      at.push_back({-3, -1});
      at.push_back({2, -2});
      at.push_back({-2, -2});
    }
    collective =
        Bitmap::create(allowance, width, dictionary.pattern_height, 0, "a collective bitmap");
    Coder coder(dictionary.coded, allowance, 0);
    decode_generic(collective, coder.decoder, coder.contexts.generic, dictionary.template_index,
                   false, nullptr, at);
  }
  BitmapList patterns;
  for (std::uint64_t i = 0; i < count; ++i) {
    patterns.push_back(std::make_shared<const Bitmap>(
        collective.columns(allowance, static_cast<std::uint32_t>(i * dictionary.pattern_width),
                           dictionary.pattern_width)));
  }
  return patterns;
}

Bitmap Jbig2Regions::halftone_region(const Jbig2Segments::HalftoneRegion& region,
                                     const BitmapList& patterns, Allowance& allowance) {
  if (patterns.empty()) {
    throw DecodeFault("a JBIG2 halftone region refers to no pattern dictionary");
  }
  const std::uint32_t region_width = region.info.width;
  const std::uint32_t region_height = region.info.height;
  Bitmap bitmap =
      Bitmap::create(allowance, region_width, region_height,
                     static_cast<std::uint8_t>(region.default_pixel), "a halftone region bitmap");
  const std::uint32_t pattern_width = patterns[0]->width();
  const std::uint32_t pattern_height = patterns[0]->height();
  const unsigned bits_per_value = ceil_log2(patterns.size());
  const std::uint32_t grid_width = region.grid_width;
  const std::uint32_t grid_height = region.grid_height;
  claim_items(allowance, static_cast<std::uint64_t>(grid_width) * grid_height,
              "the grid cells of a halftone region");

  // The pixel of the region at which the pattern of grid cell (mg, ng) is drawn
  // (6.6.5.2).
  const auto cell_x = [&](std::uint32_t mg, std::uint32_t ng) {
    return floor_div_256(static_cast<std::int64_t>(region.grid_offset_x) +
                         static_cast<std::int64_t>(mg) * region.grid_vector_y +
                         static_cast<std::int64_t>(ng) * region.grid_vector_x);
  };
  const auto cell_y = [&](std::uint32_t mg, std::uint32_t ng) {
    return floor_div_256(static_cast<std::int64_t>(region.grid_offset_y) +
                         static_cast<std::int64_t>(mg) * region.grid_vector_x -
                         static_cast<std::int64_t>(ng) * region.grid_vector_y);
  };

  std::optional<Bitmap> skip;
  if (region.enable_skip) {
    // 6.6.5.1: cells whose pattern falls wholly outside the region are skipped.
    skip = Bitmap::create(allowance, grid_width, grid_height, 0, "a halftone skip bitmap");
    for (std::uint32_t mg = 0; mg < grid_height; ++mg) {
      for (std::uint32_t ng = 0; ng < grid_width; ++ng) {
        const std::int64_t x = cell_x(mg, ng);
        const std::int64_t y = cell_y(mg, ng);
        if (std::cmp_less_equal(x + pattern_width, 0) || std::cmp_greater_equal(x, region_width) ||
            std::cmp_less_equal(y + pattern_height, 0) ||
            std::cmp_greater_equal(y, region_height)) {
          skip->set(ng, mg, 1);
        }
      }
    }
  }

  // Annex C: the gray-scale image, as bits_per_value bit planes, most
  // significant first.
  std::vector<Bitmap> planes(bits_per_value);
  if (region.mmr) {
    // The planes follow each other, each ended by its end-of-block code.
    std::size_t position = 0;
    for (unsigned i = bits_per_value; i > 0; --i) {
      const platform::ByteSpan rest = region.coded.subspan(std::min(position, region.coded.size()));
      MmrResult plane = decode_mmr(rest, grid_width, grid_height, true, allowance);
      position += plane.consumed;
      planes[i - 1] = std::move(plane.bitmap);
    }
  } else {
    std::vector<AdaptivePixel> at = {{region.template_index <= 1 ? 3 : 2, -1}};
    if (region.template_index == 0) {
      at.push_back({-3, -1});
      at.push_back({2, -2});
      at.push_back({-2, -2});
    }
    Coder coder(region.coded, allowance, 0);
    for (unsigned i = bits_per_value; i > 0; --i) {
      Bitmap plane = Bitmap::create(allowance, grid_width, grid_height, 0, "a halftone bit plane");
      decode_generic(plane, coder.decoder, coder.contexts.generic, region.template_index, false,
                     skip ? &*skip : nullptr, at);
      planes[i - 1] = std::move(plane);
    }
  }

  // 6.6.5.2: render the patterns.
  const Bitmap::Operator op = to_operator(region.combination_operator);
  for (std::uint32_t mg = 0; mg < grid_height; ++mg) {
    for (std::uint32_t ng = 0; ng < grid_width; ++ng) {
      unsigned bit = 0;
      std::uint64_t pattern_index = 0;
      for (unsigned j = bits_per_value; j > 0; --j) {
        bit ^= planes[j - 1].get(ng, mg);
        pattern_index |= static_cast<std::uint64_t>(bit) << (j - 1);
      }
      pattern_index = std::min<std::uint64_t>(pattern_index, patterns.size() - 1);
      bitmap.combine(*patterns[static_cast<std::size_t>(pattern_index)], cell_x(mg, ng),
                     cell_y(mg, ng), op, allowance);
    }
  }
  return bitmap;
}

}  // namespace pdfkit::images
