// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of images::Jbig2Decoder over streams assembled in the test from the
// segment layout of ITU-T T.88 clause 7 (recalled) and coded by a test-side MQ
// encoder, whose context templates are written out here from the standard's
// figures: a generic region under each template, with typical prediction and
// moved adaptive pixels, an MMR generic region, generic refinement, a symbol
// dictionary with a text region (arithmetic and Huffman coded, with a custom
// table and from globals), a pattern dictionary with a halftone region, the
// combination operators, a striped page, and the refusals (a page size past the
// budget, a segment type the standard leaves unassigned).

#include "images/jbig2_decoder.hpp"

#include "images/ccitt_test_encoder.hpp"
#include "images/mq_test_encoder.hpp"
#include "platform/budget.hpp"
#include "platform/byte_span.hpp"
#include "platform/error.hpp"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using pdfkit::images::Jbig2Decoder;
using pdfkit::platform::Budget;
using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;
using pdfkit::test::BitonalRows;
using pdfkit::test::CcittTestEncoder;
using pdfkit::test::MqTestEncoder;

using Bytes = std::vector<std::byte>;

// ---------------------------------------------------------------------------
// Segment assembly (T.88 7.2 and 7.4).

void put8(Bytes& out, unsigned value) {
  out.push_back(static_cast<std::byte>(value));
}

void put16(Bytes& out, unsigned value) {
  put8(out, value >> 8U);
  put8(out, value & 0xffU);
}

void put32(Bytes& out, std::uint32_t value) {
  put16(out, value >> 16U);
  put16(out, value & 0xffffU);
}

void append(Bytes& out, const Bytes& more) {
  out.insert(out.end(), more.begin(), more.end());
}

Bytes segment(std::uint32_t number, unsigned type, const std::vector<std::uint32_t>& referred,
              const Bytes& data) {
  Bytes out;
  put32(out, number);
  put8(out, type);
  put8(out, static_cast<unsigned>(referred.size()) << 5U);
  for (const std::uint32_t value : referred) {
    put8(out, value);
  }
  put8(out, 1);  // page association
  put32(out, static_cast<std::uint32_t>(data.size()));
  append(out, data);
  return out;
}

Bytes page_information(std::uint32_t width, std::uint32_t height, unsigned flags = 0) {
  Bytes data;
  put32(data, width);
  put32(data, height);
  put32(data, 0);
  put32(data, 0);
  put8(data, flags);
  put16(data, 0);
  return data;
}

Bytes region_information(std::uint32_t width, std::uint32_t height, std::uint32_t x,
                         std::uint32_t y, unsigned op = 0) {
  Bytes data;
  put32(data, width);
  put32(data, height);
  put32(data, x);
  put32(data, y);
  put8(data, op);
  return data;
}

// ---------------------------------------------------------------------------
// Test bitmaps, true for black.

BitonalRows pattern(std::size_t width, std::size_t height, std::uint32_t seed, unsigned density) {
  BitonalRows rows(height, std::vector<bool>(width, false));
  std::uint32_t state = seed;
  for (std::size_t y = 0; y < height; ++y) {
    for (std::size_t x = 0; x < width; ++x) {
      state = state * 1664525U + 1013904223U;
      bool black = ((state >> 24U) % 100U) < density;
      if (y % 7 < 2) {
        black = (x / 3) % 2 == 0;  // runs, so typical rows exist
      }
      rows[y][x] = black;
    }
  }
  // Two identical rows in a row, for typical prediction to find.
  if (height > 12) {
    rows[11] = rows[10];
  }
  return rows;
}

bool at(const BitonalRows& rows, long x, long y) {
  if (y < 0 || x < 0 || static_cast<std::size_t>(y) >= rows.size() ||
      static_cast<std::size_t>(x) >= rows[static_cast<std::size_t>(y)].size()) {
    return false;
  }
  return rows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
}

// ---------------------------------------------------------------------------
// The generic region coder, written from the figures of T.88 6.2.5.3.

struct Pt {
  int dx;
  int dy;
};

// The context template of T.88 figures 3 to 6, in label order from the most
// significant bit, with the adaptive pixels at their nominal slots.
std::vector<Pt> context_template(unsigned t, const std::vector<Pt>& adaptive) {
  switch (t) {
    case 0:
      return {adaptive[3], {-1, -2}, {0, -2}, {1, -2}, adaptive[2], adaptive[1],
              {-2, -1},    {-1, -1}, {0, -1}, {1, -1}, {2, -1},     adaptive[0],
              {-4, 0},     {-3, 0},  {-2, 0}, {-1, 0}};
    case 1:
      return {{-1, -2}, {0, -2}, {1, -2},     {2, -2}, {-2, -1}, {-1, -1}, {0, -1},
              {1, -1},  {2, -1}, adaptive[0], {-3, 0}, {-2, 0},  {-1, 0}};
    case 2:
      return {{-1, -2}, {0, -2}, {1, -2},     {-2, -1}, {-1, -1},
              {0, -1},  {1, -1}, adaptive[0], {-2, 0},  {-1, 0}};
    default:
      return {{-3, -1},    {-2, -1}, {-1, -1}, {0, -1}, {1, -1},
              adaptive[0], {-4, 0},  {-3, 0},  {-2, 0}, {-1, 0}};
  }
}

std::vector<Pt> default_adaptive(unsigned t) {
  switch (t) {
    case 0:
      return {{3, -1}, {-3, -1}, {2, -2}, {-2, -2}};
    case 1:
      return {{3, -1}};
    default:
      return {{2, -1}};
  }
}

constexpr std::array<std::uint32_t, 4> k_sltp_context = {0x9B25, 0x0795, 0x00E5, 0x0195};

// Encodes the pixels of rows into encoder under the generic region procedure.
void encode_generic(MqTestEncoder& encoder, std::vector<std::uint8_t>& contexts,
                    const BitonalRows& rows, unsigned t, bool tpgdon,
                    const std::vector<Pt>& adaptive) {
  const std::vector<Pt> shape = context_template(t, adaptive);
  unsigned ltp = 0;
  for (std::size_t y = 0; y < rows.size(); ++y) {
    if (tpgdon) {
      const bool same = y == 0
                            ? std::none_of(rows[0].begin(), rows[0].end(), [](bool b) { return b; })
                            : rows[y] == rows[y - 1];
      const unsigned next_ltp = same ? 1U : 0U;
      encoder.encode(contexts[k_sltp_context[t]], next_ltp ^ ltp);
      ltp = next_ltp;
      if (ltp != 0) {
        continue;
      }
    }
    for (std::size_t x = 0; x < rows[y].size(); ++x) {
      std::uint32_t label = 0;
      for (const Pt& p : shape) {
        label = (label << 1U) |
                (at(rows, static_cast<long>(x) + p.dx, static_cast<long>(y) + p.dy) ? 1U : 0U);
      }
      encoder.encode(contexts[label], rows[y][x] ? 1U : 0U);
    }
  }
}

Bytes generic_region_data(const BitonalRows& rows, unsigned t, bool tpgdon,
                          const std::vector<Pt>& adaptive, std::uint32_t x = 0, std::uint32_t y = 0,
                          unsigned op = 0) {
  Bytes data = region_information(static_cast<std::uint32_t>(rows[0].size()),
                                  static_cast<std::uint32_t>(rows.size()), x, y, op);
  put8(data, (t << 1U) | (tpgdon ? 8U : 0U));
  for (const Pt& p : adaptive) {
    put8(data, static_cast<unsigned>(p.dx) & 0xffU);
    put8(data, static_cast<unsigned>(p.dy) & 0xffU);
  }
  MqTestEncoder encoder;
  std::vector<std::uint8_t> contexts(std::size_t{1} << 16U, 0);
  encode_generic(encoder, contexts, rows, t, tpgdon, adaptive);
  append(data, encoder.finish());
  return data;
}

// ---------------------------------------------------------------------------
// Decoding and comparing.

Jbig2Decoder::Result decode(const Bytes& data, const Bytes& globals = {}) {
  Budget budget;
  return Jbig2Decoder::decode(data, globals, budget, "object 9 0");
}

// Whether the pixel (x, y) of the packed result is black (a 0 bit).
bool black(const Jbig2Decoder::Result& result, std::size_t x, std::size_t y) {
  const std::size_t row_bytes = (result.width + 7U) / 8U;
  const auto byte = std::to_integer<unsigned>(result.data[y * row_bytes + x / 8]);
  return ((byte >> (7U - x % 8U)) & 1U) == 0;
}

void check_page(const Jbig2Decoder::Result& result, const BitonalRows& expected) {
  REQUIRE(result.fault.empty());
  REQUIRE(result.height == expected.size());
  REQUIRE(result.width == expected[0].size());
  REQUIRE(result.data.size() == ((result.width + 7U) / 8U) * result.height);
  for (std::size_t y = 0; y < expected.size(); ++y) {
    for (std::size_t x = 0; x < expected[y].size(); ++x) {
      if (black(result, x, y) != expected[y][x]) {
        FAIL("pixel (" << x << ", " << y << ") differs");
      }
    }
  }
}

Bytes page_with(const Bytes& region_segments, std::uint32_t width, std::uint32_t height,
                unsigned page_flags = 0) {
  Bytes out = segment(0, 48, {}, page_information(width, height, page_flags));
  append(out, region_segments);
  return out;
}

TEST_CASE("a generic region round trips under each template, with and without typical prediction") {
  const BitonalRows image = pattern(37, 29, 5, 35);
  for (unsigned t = 0; t < 4; ++t) {
    for (const bool tpgdon : {false, true}) {
      const auto data = page_with(
          segment(1, 38, {}, generic_region_data(image, t, tpgdon, default_adaptive(t))), 37, 29);
      INFO("template " << t << " tpgdon " << tpgdon);
      check_page(decode(data), image);
    }
  }
}

TEST_CASE("a generic region round trips with the adaptive template pixels moved") {
  const BitonalRows image = pattern(41, 25, 8, 30);
  const std::vector<std::vector<Pt>> placements = {
      {{4, -1}, {-4, -1}, {1, -2}, {-1, -2}},  // template 0
      {{-2, -2}},                              // template 1
      {{3, -2}},                               // template 2
      {{-5, 0}},                               // template 3
  };
  for (unsigned t = 0; t < 4; ++t) {
    for (const bool tpgdon : {false, true}) {
      const auto data = page_with(
          segment(1, 38, {}, generic_region_data(image, t, tpgdon, placements[t])), 41, 25);
      INFO("template " << t << " tpgdon " << tpgdon);
      check_page(decode(data), image);
    }
  }
}

TEST_CASE("a generic region lands at its position on a page whose default pixel is set") {
  const BitonalRows image = pattern(12, 9, 3, 50);
  // Page default pixel 1 (black): the region ORs over it, leaving black.
  const auto black_page =
      page_with(segment(1, 38, {}, generic_region_data(image, 0, false, default_adaptive(0), 5, 4)),
                30, 20, 4);
  const auto result = decode(black_page);
  REQUIRE(result.fault.empty());
  for (std::size_t y = 0; y < 20; ++y) {
    for (std::size_t x = 0; x < 30; ++x) {
      CHECK(black(result, x, y));
    }
  }
  // Page default pixel 0: the region shows at (5, 4).
  const auto white_page = page_with(
      segment(1, 38, {}, generic_region_data(image, 0, false, default_adaptive(0), 5, 4)), 30, 20);
  BitonalRows expected(20, std::vector<bool>(30, false));
  for (std::size_t y = 0; y < 9; ++y) {
    for (std::size_t x = 0; x < 12; ++x) {
      expected[y + 4][x + 5] = image[y][x];
    }
  }
  check_page(decode(white_page), expected);
}

TEST_CASE("an MMR generic region decodes through the CCITT Group 4 decoder") {
  const BitonalRows image = pattern(45, 21, 12, 25);
  Bytes data = region_information(45, 21, 0, 0);
  put8(data, 1);  // MMR
  CcittTestEncoder::Options options;
  options.k = -1;
  append(data, CcittTestEncoder::encode(image, options));
  check_page(decode(page_with(segment(1, 38, {}, data), 45, 21)), image);
}

TEST_CASE("the combination operators of a region apply when the page allows the override") {
  const BitonalRows a = pattern(20, 14, 1, 40);
  const BitonalRows b = pattern(20, 14, 2, 40);
  struct Case {
    unsigned op;
    bool (*combine)(bool, bool);
  };
  const Case cases[] = {{0, [](bool x, bool y) { return x || y; }},
                        {1, [](bool x, bool y) { return x && y; }},
                        {2, [](bool x, bool y) { return x != y; }},
                        {3, [](bool x, bool y) { return x == y; }},
                        {4, [](bool, bool y) { return y; }}};
  for (const Case& c : cases) {
    Bytes regions = segment(1, 38, {}, generic_region_data(a, 0, false, default_adaptive(0)));
    append(regions,
           segment(2, 38, {}, generic_region_data(b, 0, false, default_adaptive(0), 0, 0, c.op)));
    BitonalRows expected = a;
    for (std::size_t y = 0; y < a.size(); ++y) {
      for (std::size_t x = 0; x < a[y].size(); ++x) {
        expected[y][x] = c.combine(a[y][x], b[y][x]);
      }
    }
    INFO("operator " << c.op);
    check_page(decode(page_with(regions, 20, 14, 64)), expected);
  }
}

TEST_CASE("a striped page of unknown height grows with its regions and stripes") {
  const BitonalRows top = pattern(24, 10, 4, 40);
  const BitonalRows bottom = pattern(24, 6, 6, 40);
  Bytes out = segment(0, 48, {}, page_information(24, 0xFFFFFFFFU, 0));
  append(out, segment(1, 38, {}, generic_region_data(top, 0, false, default_adaptive(0), 0, 0)));
  Bytes stripe;
  put32(stripe, 9);
  append(out, segment(2, 50, {}, stripe));
  append(out,
         segment(3, 38, {}, generic_region_data(bottom, 0, false, default_adaptive(0), 0, 10)));
  Bytes stripe_end;
  put32(stripe_end, 15);
  append(out, segment(4, 50, {}, stripe_end));
  BitonalRows expected = top;
  expected.insert(expected.end(), bottom.begin(), bottom.end());
  check_page(decode(out), expected);
}

// ---------------------------------------------------------------------------
// Generic refinement (T.88 6.3).

struct RefinementShape {
  std::vector<Pt> coding;
  std::vector<Pt> reference;
};

RefinementShape refinement_shape(unsigned t) {
  if (t == 0) {
    return {{{0, -1}, {1, -1}, {-1, 0}, {-1, -1}},
            {{0, -1}, {1, -1}, {-1, 0}, {0, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}, {-1, -1}}};
  }
  return {{{-1, -1}, {0, -1}, {1, -1}, {-1, 0}},
          {{0, -1}, {-1, 0}, {0, 0}, {1, 0}, {0, 1}, {1, 1}}};
}

void encode_refinement(MqTestEncoder& encoder, std::vector<std::uint8_t>& contexts,
                       const BitonalRows& target, const BitonalRows& reference, long offset_x,
                       long offset_y, unsigned t, bool tpgron) {
  const RefinementShape shape = refinement_shape(t);
  const std::uint32_t sltp_context = t == 0 ? 0x0020U : 0x0008U;
  const auto uniform = [&](long rx, long ry) {
    const bool centre = at(reference, rx, ry);
    for (long dy = -1; dy <= 1; ++dy) {
      for (long dx = -1; dx <= 1; ++dx) {
        if (at(reference, rx + dx, ry + dy) != centre) {
          return false;
        }
      }
    }
    return true;
  };
  unsigned ltp = 0;
  for (std::size_t y = 0; y < target.size(); ++y) {
    const long ry = static_cast<long>(y) - offset_y;
    if (tpgron) {
      bool typical = true;
      for (std::size_t x = 0; x < target[y].size(); ++x) {
        const long rx = static_cast<long>(x) - offset_x;
        if (uniform(rx, ry) && target[y][x] != at(reference, rx, ry)) {
          typical = false;
        }
      }
      const unsigned next_ltp = typical ? 1U : 0U;
      encoder.encode(contexts[sltp_context], next_ltp ^ ltp);
      ltp = next_ltp;
    }
    for (std::size_t x = 0; x < target[y].size(); ++x) {
      const long rx = static_cast<long>(x) - offset_x;
      if (ltp != 0 && uniform(rx, ry)) {
        continue;
      }
      std::uint32_t label = 0;
      for (const Pt& p : shape.coding) {
        label = (label << 1U) |
                (at(target, static_cast<long>(x) + p.dx, static_cast<long>(y) + p.dy) ? 1U : 0U);
      }
      for (const Pt& p : shape.reference) {
        label = (label << 1U) | (at(reference, rx + p.dx, ry + p.dy) ? 1U : 0U);
      }
      encoder.encode(contexts[label], target[y][x] ? 1U : 0U);
    }
  }
}

TEST_CASE("a generic refinement region refines the page area it covers") {
  const BitonalRows base = pattern(26, 18, 9, 35);
  BitonalRows target = base;
  for (std::size_t y = 3; y < 15; ++y) {
    target[y][(y * 5) % 26] = !target[y][(y * 5) % 26];
  }
  for (unsigned t = 0; t < 2; ++t) {
    for (const bool tpgron : {false, true}) {
      Bytes refinement = region_information(26, 18, 0, 0, 4);
      put8(refinement, t | (tpgron ? 2U : 0U));
      if (t == 0) {
        for (const int value : {-1, -1, -1, -1}) {
          put8(refinement, static_cast<unsigned>(value) & 0xffU);
        }
      }
      MqTestEncoder encoder;
      std::vector<std::uint8_t> contexts(std::size_t{1} << 16U, 0);
      encode_refinement(encoder, contexts, target, base, 0, 0, t, tpgron);
      append(refinement, encoder.finish());
      Bytes regions = segment(1, 38, {}, generic_region_data(base, 0, false, default_adaptive(0)));
      append(regions, segment(2, 42, {}, refinement));
      INFO("template " << t << " tpgron " << tpgron);
      check_page(decode(page_with(regions, 26, 18, 64)), target);
    }
  }
}

// ---------------------------------------------------------------------------
// Symbol dictionaries and text regions.

unsigned ceil_log2(std::size_t value) {
  unsigned bits = 0;
  while ((std::size_t{1} << bits) < value) {
    ++bits;
  }
  return bits;
}

struct ArithmeticContexts {
  std::vector<std::uint8_t> generic = std::vector<std::uint8_t>(std::size_t{1} << 16U, 0);
  MqTestEncoder::IntegerContexts delta_height{};
  MqTestEncoder::IntegerContexts delta_width{};
  MqTestEncoder::IntegerContexts export_run{};
  MqTestEncoder::IntegerContexts delta_t{};
  MqTestEncoder::IntegerContexts first_s{};
  MqTestEncoder::IntegerContexts delta_s{};
  std::vector<std::uint8_t> symbol_id;
};

// A symbol dictionary of symbols grouped by height (ascending), template 0.
Bytes symbol_dictionary_data(const std::vector<BitonalRows>& symbols, unsigned exported_from) {
  Bytes data;
  put16(data, 0);  // arithmetic, no refinement, template 0
  for (const Pt& p : default_adaptive(0)) {
    put8(data, static_cast<unsigned>(p.dx) & 0xffU);
    put8(data, static_cast<unsigned>(p.dy) & 0xffU);
  }
  put32(data, static_cast<std::uint32_t>(symbols.size() - exported_from));
  put32(data, static_cast<std::uint32_t>(symbols.size()));
  MqTestEncoder encoder;
  ArithmeticContexts contexts;
  std::size_t height = 0;
  std::size_t i = 0;
  while (i < symbols.size()) {
    const std::size_t class_height = symbols[i].size();
    encoder.encode_integer(contexts.delta_height, static_cast<std::int64_t>(class_height) -
                                                      static_cast<std::int64_t>(height));
    height = class_height;
    std::size_t width = 0;
    while (i < symbols.size() && symbols[i].size() == class_height) {
      const std::size_t symbol_width = symbols[i][0].size();
      encoder.encode_integer(contexts.delta_width, static_cast<std::int64_t>(symbol_width) -
                                                       static_cast<std::int64_t>(width));
      width = symbol_width;
      encode_generic(encoder, contexts.generic, symbols[i], 0, false, default_adaptive(0));
      ++i;
    }
    encoder.encode_integer(contexts.delta_width, std::nullopt);
  }
  // Export flags: the first exported_from symbols are not exported.
  encoder.encode_integer(contexts.export_run, static_cast<std::int64_t>(exported_from));
  encoder.encode_integer(contexts.export_run,
                         static_cast<std::int64_t>(symbols.size() - exported_from));
  append(data, encoder.finish());
  return data;
}

struct Instance {
  std::size_t symbol;
  long s;
  long t;
};

// A text region of instances in strips of height 1, reference corner top left.
Bytes text_region_data(std::uint32_t width, std::uint32_t height,
                       const std::vector<BitonalRows>& symbols,
                       const std::vector<Instance>& instances) {
  Bytes data = region_information(width, height, 0, 0);
  put16(data, 1U << 4U);  // arithmetic, reference corner top left
  put32(data, static_cast<std::uint32_t>(instances.size()));
  MqTestEncoder encoder;
  ArithmeticContexts contexts;
  const unsigned code_length = ceil_log2(symbols.size());
  contexts.symbol_id.assign(std::size_t{1} << (code_length + 1), 0);
  encoder.encode_integer(contexts.delta_t, 0);  // the initial strip T is 0
  long strip_t = 0;
  long first_s = 0;
  std::size_t i = 0;
  while (i < instances.size()) {
    // One strip per run of instances sharing a T.
    const long t = instances[i].t;
    encoder.encode_integer(contexts.delta_t, t - strip_t);
    strip_t = t;
    encoder.encode_integer(contexts.first_s, instances[i].s - first_s);
    first_s = instances[i].s;
    long current_s = first_s;
    while (true) {
      const Instance& instance = instances[i];
      encoder.encode_iaid(contexts.symbol_id, code_length,
                          static_cast<std::uint32_t>(instance.symbol));
      current_s += static_cast<long>(symbols[instance.symbol][0].size()) - 1;
      ++i;
      if (i < instances.size() && instances[i].t == t) {
        encoder.encode_integer(contexts.delta_s, instances[i].s - current_s);
        current_s = instances[i].s;
      } else {
        encoder.encode_integer(contexts.delta_s, std::nullopt);
        break;
      }
    }
  }
  append(data, encoder.finish());
  return data;
}

std::vector<BitonalRows> sample_symbols() {
  std::vector<BitonalRows> symbols;
  symbols.push_back(pattern(5, 5, 21, 50));  // height class 5
  symbols.push_back(pattern(4, 5, 22, 50));
  symbols.push_back(pattern(6, 8, 23, 50));  // height class 8
  return symbols;
}

BitonalRows draw(std::size_t width, std::size_t height, const std::vector<BitonalRows>& symbols,
                 const std::vector<Instance>& instances) {
  BitonalRows page(height, std::vector<bool>(width, false));
  for (const Instance& instance : instances) {
    const BitonalRows& symbol = symbols[instance.symbol];
    for (std::size_t y = 0; y < symbol.size(); ++y) {
      for (std::size_t x = 0; x < symbol[y].size(); ++x) {
        if (symbol[y][x]) {
          page[static_cast<std::size_t>(instance.t) + y][static_cast<std::size_t>(instance.s) + x] =
              true;
        }
      }
    }
  }
  return page;
}

TEST_CASE("a text region draws the symbols of an arithmetic symbol dictionary") {
  const auto symbols = sample_symbols();
  const std::vector<Instance> instances = {
      {2, 1, 2}, {0, 9, 2}, {1, 3, 10}, {2, 12, 10}, {0, 20, 10}};
  Bytes regions = segment(1, 0, {}, symbol_dictionary_data(symbols, 0));
  append(regions, segment(2, 6, {1}, text_region_data(30, 20, symbols, instances)));
  check_page(decode(page_with(regions, 30, 20)), draw(30, 20, symbols, instances));
}

TEST_CASE("a symbol dictionary exports only the symbols its flags select") {
  const auto symbols = sample_symbols();
  // The first symbol is not exported, so the region's symbol 0 is the second.
  const std::vector<BitonalRows> exported(symbols.begin() + 1, symbols.end());
  const std::vector<Instance> instances = {{0, 2, 3}, {1, 10, 3}};
  Bytes regions = segment(1, 0, {}, symbol_dictionary_data(symbols, 1));
  append(regions, segment(2, 6, {1}, text_region_data(24, 16, exported, instances)));
  check_page(decode(page_with(regions, 24, 16)), draw(24, 16, exported, instances));
}

TEST_CASE("the symbols of /JBIG2Globals are available to the image's segments") {
  const auto symbols = sample_symbols();
  const std::vector<Instance> instances = {{1, 4, 4}, {2, 14, 4}};
  const Bytes globals = segment(1, 0, {}, symbol_dictionary_data(symbols, 0));
  Bytes image = segment(0, 48, {}, page_information(26, 14));
  append(image, segment(2, 6, {1}, text_region_data(26, 14, symbols, instances)));
  check_page(decode(image, globals), draw(26, 14, symbols, instances));
}

// ---------------------------------------------------------------------------
// Huffman-coded symbol dictionaries and text regions, assembled bit by bit
// from the standard tables of T.88 annex B.

class BitString {
 public:
  BitString& bits(const std::string& text) {
    for (const char c : text) {
      bits_.push_back(c == '1');
    }
    return *this;
  }
  BitString& align() {
    while (bits_.size() % 8 != 0) {
      bits_.push_back(false);
    }
    return *this;
  }
  BitString& byte(unsigned value) {
    for (unsigned i = 8; i > 0; --i) {
      bits_.push_back(((value >> (i - 1U)) & 1U) != 0);
    }
    return *this;
  }
  [[nodiscard]] Bytes bytes() const {
    Bytes out((bits_.size() + 7) / 8, std::byte{0});
    for (std::size_t i = 0; i < bits_.size(); ++i) {
      if (bits_[i]) {
        out[i / 8] |= static_cast<std::byte>(0x80U >> (i % 8));
      }
    }
    return out;
  }

 private:
  std::vector<bool> bits_;
};

// Two symbols of height 5: widths 3 and 5, in an uncompressed collective bitmap.
Bytes huffman_symbol_dictionary(const BitonalRows& first, const BitonalRows& second) {
  Bytes data;
  put16(data, 1);  // Huffman, table B.4 for heights and B.2 for widths
  put32(data, 2);
  put32(data, 2);
  BitString bits;
  bits.bits("1110001");  // B.4: height 5
  bits.bits("1110000");  // B.2: width 3
  bits.bits("110");      // B.2: width + 2
  bits.bits("111111");   // B.2: out-of-band
  bits.bits("00000");    // B.1: the collective bitmap is uncompressed (size 0)
  bits.align();
  for (std::size_t y = 0; y < 5; ++y) {
    unsigned value = 0;
    for (std::size_t x = 0; x < 8; ++x) {
      const bool pixel = x < 3 ? first[y][x] : second[y][x - 3];
      value = (value << 1U) | (pixel ? 1U : 0U);
    }
    bits.byte(value);
  }
  bits.bits("00000");  // B.1: export run 0
  bits.bits("00010");  // B.1: export run 2
  append(data, bits.bytes());
  return data;
}

TEST_CASE("a Huffman text region draws the symbols of a Huffman symbol dictionary") {
  const BitonalRows first = pattern(3, 5, 31, 50);
  const BitonalRows second = pattern(5, 5, 32, 50);
  Bytes text = region_information(16, 12, 0, 0);
  put16(text, 1U | (1U << 4U));  // Huffman, reference corner top left
  put16(text, 0);                // tables B.6, B.8 and B.11
  put32(text, 2);
  BitString bits;
  // The symbol ID table: run code 1 has length 1, the others none, and both
  // symbols have the code length 1, so symbol 0 is the code 0 and symbol 1 is 1.
  bits.bits("0000");
  bits.bits("0001");
  for (int i = 0; i < 33; ++i) {
    bits.bits("0000");
  }
  bits.bits("00");
  bits.align();
  bits.bits("0");          // B.11: initial strip T = -1
  bits.bits("1100");       // B.11: delta T 4, so the strip is at T = 3
  bits.bits("000000010");  // B.6: first S = 2
  bits.bits("1");          // symbol ID 1
  bits.bits("001");        // B.8: delta S 1
  bits.bits("0");          // symbol ID 0
  bits.bits("01");         // B.8: out-of-band
  append(text, bits.bytes());
  Bytes regions = segment(1, 0, {}, huffman_symbol_dictionary(first, second));
  append(regions, segment(2, 6, {1}, text));

  BitonalRows expected(12, std::vector<bool>(16, false));
  for (std::size_t y = 0; y < 5; ++y) {
    for (std::size_t x = 0; x < 5; ++x) {
      expected[3 + y][2 + x] = second[y][x];
    }
    // The second instance starts at S = 2 + 5 - 1 + 1 = 7.
    for (std::size_t x = 0; x < 3; ++x) {
      expected[3 + y][7 + x] = first[y][x];
    }
  }
  check_page(decode(page_with(regions, 16, 12)), expected);
}

TEST_CASE("a Huffman text region reads its delta T from a custom table of a Tables segment") {
  const BitonalRows first = pattern(3, 5, 31, 50);
  const BitonalRows second = pattern(5, 5, 32, 50);
  // Table lines: 0 to 1 coded 0 plus one bit, 2 to 3 coded 10 plus one bit,
  // an upper range line coded 11, no lower range line and no out-of-band line.
  Bytes table;
  put8(table, 0x02);  // two-bit prefix lengths, one-bit range lengths
  put32(table, 0);
  put32(table, 4);
  BitString table_bits;
  table_bits.bits("01").bits("1");
  table_bits.bits("10").bits("1");
  table_bits.bits("00");
  table_bits.bits("10");
  append(table, table_bits.bytes());

  Bytes text = region_information(16, 12, 0, 0);
  put16(text, 1U | (1U << 4U));
  put16(text, 3U << 4U);  // delta T from a custom table
  put32(text, 2);
  BitString bits;
  bits.bits("0000").bits("0001");
  for (int i = 0; i < 33; ++i) {
    bits.bits("0000");
  }
  bits.bits("00");
  bits.align();
  bits.bits("01");         // custom table: initial strip T = -1
  bits.bits("101");        // custom table: delta T 3, so the strip is at T = 2
  bits.bits("000000010");  // B.6: first S = 2
  bits.bits("1");
  bits.bits("001");
  bits.bits("0");
  bits.bits("01");
  append(text, bits.bytes());
  Bytes regions = segment(5, 53, {}, table);
  append(regions, segment(1, 0, {}, huffman_symbol_dictionary(first, second)));
  append(regions, segment(2, 6, {1, 5}, text));

  BitonalRows expected(12, std::vector<bool>(16, false));
  for (std::size_t y = 0; y < 5; ++y) {
    for (std::size_t x = 0; x < 5; ++x) {
      expected[2 + y][2 + x] = second[y][x];
    }
    for (std::size_t x = 0; x < 3; ++x) {
      expected[2 + y][7 + x] = first[y][x];
    }
  }
  check_page(decode(page_with(regions, 16, 12)), expected);
}

// ---------------------------------------------------------------------------
// Pattern dictionaries and halftone regions.

TEST_CASE("a halftone region draws the patterns of a pattern dictionary on its grid") {
  const unsigned pattern_size = 4;
  const BitonalRows collective = pattern(std::size_t{2} * pattern_size, pattern_size, 41, 50);
  Bytes dictionary;
  put8(dictionary, 0);  // arithmetic, template 0
  put8(dictionary, pattern_size);
  put8(dictionary, pattern_size);
  put32(dictionary, 1);  // two patterns
  {
    MqTestEncoder encoder;
    std::vector<std::uint8_t> contexts(std::size_t{1} << 16U, 0);
    encode_generic(encoder, contexts, collective, 0, false,
                   {{-static_cast<int>(pattern_size), 0}, {-3, -1}, {2, -2}, {-2, -2}});
    append(dictionary, encoder.finish());
  }
  // A grid of 3 by 2 cells choosing a pattern each, one bit plane.
  const BitonalRows grid = {{true, false, true}, {false, true, true}};
  Bytes halftone = region_information(12, 8, 0, 0);
  put8(halftone, 0);  // arithmetic, template 0, no skip, OR, default pixel 0
  put32(halftone, 3);
  put32(halftone, 2);
  put32(halftone, 0);
  put32(halftone, 0);
  put16(halftone, 4U * 256U);  // the grid vector's x step: one pattern width
  put16(halftone, 0);
  {
    MqTestEncoder encoder;
    std::vector<std::uint8_t> contexts(std::size_t{1} << 16U, 0);
    encode_generic(encoder, contexts, grid, 0, false, {{3, -1}, {-3, -1}, {2, -2}, {-2, -2}});
    append(halftone, encoder.finish());
  }
  Bytes regions = segment(1, 16, {}, dictionary);
  append(regions, segment(2, 22, {1}, halftone));

  BitonalRows expected(8, std::vector<bool>(12, false));
  for (std::size_t mg = 0; mg < 2; ++mg) {
    for (std::size_t ng = 0; ng < 3; ++ng) {
      const std::size_t index = grid[mg][ng] ? 1 : 0;
      // The grid's x vector is (4, 0) pixels, so cell (mg, ng) is drawn at
      // x = ng * 4 and y = mg * 4.
      for (std::size_t y = 0; y < pattern_size; ++y) {
        for (std::size_t x = 0; x < pattern_size; ++x) {
          if (collective[y][index * pattern_size + x]) {
            expected[mg * pattern_size + y][ng * pattern_size + x] = true;
          }
        }
      }
    }
  }
  check_page(decode(page_with(regions, 12, 8)), expected);
}

// ---------------------------------------------------------------------------
// Refusals.

TEST_CASE("a page size past the budget is refused before the bitmap is reserved") {
  const auto data = page_with(Bytes(), 100000, 100000);
  Budget budget;
  try {
    const auto result = Jbig2Decoder::decode(data, {}, budget, "object 9 0");
    FAIL("the claim was accepted");
  } catch (const Error& error) {
    CHECK(error.code() == ErrorCode::limit_exceeded);
  }
}

TEST_CASE("a region size past the budget is refused") {
  Bytes region = region_information(200000, 200000, 0, 0);
  put8(region, 0);
  for (const int value : {3, -1, -3, -1, 2, -2, -2, -2}) {
    put8(region, static_cast<unsigned>(value) & 0xffU);
  }
  const auto data = page_with(segment(1, 38, {}, region), 8, 8);
  Budget budget;
  CHECK_THROWS_AS(const auto discarded = Jbig2Decoder::decode(data, {}, budget, "object 9 0"),
                  Error);
}

TEST_CASE("a segment of an unassigned type is a fault that names the type") {
  Bytes data = segment(0, 48, {}, page_information(8, 8));
  append(data, segment(1, 3, {}, Bytes()));
  const auto result = decode(data);
  CHECK(result.data.empty());
  CHECK(result.fault.find("type 3") != std::string::npos);
}

TEST_CASE("data that holds no page information is a fault") {
  const auto result = decode(
      segment(1, 38, {}, generic_region_data(pattern(8, 8, 1, 50), 0, false, default_adaptive(0))));
  CHECK_FALSE(result.fault.empty());
}

// Decoding any bytes either gives a page, or a fault with no data, or a
// refusal by the budget; it never crashes or hangs.
bool survives(const Bytes& data) {
  try {
    const auto result = decode(data);
    return result.fault.empty() || result.data.empty();
  } catch (const Error& error) {
    return error.code() == ErrorCode::limit_exceeded;
  }
}

TEST_CASE("truncated and garbage streams give a fault or a page, never a crash") {
  const BitonalRows image = pattern(33, 17, 14, 40);
  const auto full = page_with(
      segment(1, 38, {}, generic_region_data(image, 1, true, default_adaptive(1))), 33, 17);
  for (std::size_t cut = 0; cut < full.size(); cut += 3) {
    const Bytes part(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(cut));
    CHECK(survives(part));
  }
  Bytes garbage(600);
  std::uint32_t state = 77;
  for (std::byte& value : garbage) {
    state = state * 1103515245U + 12345U;
    value = static_cast<std::byte>(state >> 16U);
  }
  for (std::size_t start = 0; start < 64; ++start) {
    const Bytes part(garbage.begin() + static_cast<std::ptrdiff_t>(start), garbage.end());
    CHECK(survives(part));
  }
}

}  // namespace
