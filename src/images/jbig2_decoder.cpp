// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The JBIG2 decoder's entry: the segments of /JBIG2Globals and of the image are
// processed in order against one page bitmap, as the visitor of pdf.js
// src/core/jbig2.js at commit fe48736846e2043696738bd54435ebb5ab24ecff (read;
// Apache-2.0) does, and the result is the page with its bits inverted, as
// pdf.js's Jbig2Stream returns it (src/core/jbig2_stream.js at the same
// commit), so a 0 bit is black. Beyond pdf.js, intermediate regions and
// generic refinement regions are decoded (T.88 7.4.7), all five combination
// operators apply, and a page of unknown height (a striped page, T.88 7.4.8)
// grows with its regions and its end-of-stripe segments. A damaged stream
// throws DecodeFault inside the layer; it is caught here and becomes the
// result's fault, with no data, because a page whose segments did not all
// decode is not the page the file holds.

#include "images/jbig2_decoder.hpp"

#include "images/decode_fault.hpp"
#include "images/jbig2_bitmap.hpp"
#include "images/jbig2_huffman.hpp"
#include "images/jbig2_regions.hpp"
#include "images/jbig2_segments.hpp"
#include "platform/checked.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pdfkit::images {

namespace {

constexpr std::uint32_t k_unknown_height = 0xFFFFFFFFU;

class Page {
 public:
  Page(Jbig2Bitmap::Allowance& allowance, const Jbig2Segments::PageInfo& info)
      : allowance_(&allowance), info_(info) {
    height_ = info.height == k_unknown_height ? 0 : info.height;
    bitmap_ = Jbig2Bitmap::create(allowance, info.width, height_,
                                  static_cast<std::uint8_t>(info.default_pixel), "the page bitmap");
  }

  [[nodiscard]] const Jbig2Segments::PageInfo& info() const noexcept { return info_; }
  [[nodiscard]] std::uint32_t width() const noexcept { return info_.width; }
  [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
  [[nodiscard]] const Jbig2Bitmap& bitmap() const noexcept { return bitmap_; }

  // A striped page grows to hold what is drawn on it.
  void ensure_height(std::uint64_t height) {
    if (info_.height != k_unknown_height || height <= height_) {
      return;
    }
    if (height > 0x7fffffffU) {
      throw DecodeFault("a JBIG2 page grows past 2 billion rows");
    }
    Jbig2Bitmap grown =
        Jbig2Bitmap::create(*allowance_, info_.width, static_cast<std::uint32_t>(height),
                            static_cast<std::uint8_t>(info_.default_pixel), "the page bitmap");
    grown.combine(bitmap_, 0, 0, Jbig2Bitmap::Operator::replace);
    bitmap_ = std::move(grown);
    height_ = static_cast<std::uint32_t>(height);
  }

  void draw(const Jbig2Segments::RegionInfo& region, const Jbig2Bitmap& bitmap) {
    ensure_height(static_cast<std::uint64_t>(region.y) + bitmap.height());
    const unsigned code = info_.combination_operator_override ? region.combination_operator
                                                              : info_.combination_operator;
    bitmap_.combine(bitmap, region.x, region.y, to_operator(code));
  }

  // The part of the page a refinement region covers.
  [[nodiscard]] Jbig2Bitmap crop(const Jbig2Segments::RegionInfo& region) const {
    Jbig2Bitmap out = Jbig2Bitmap::create(*allowance_, region.width, region.height, 0,
                                          "a region read back from the page");
    for (std::uint32_t y = 0; y < region.height; ++y) {
      auto row = out.row(y);
      for (std::uint32_t x = 0; x < region.width; ++x) {
        row[x] = bitmap_.get(static_cast<std::int64_t>(region.x) + x,
                             static_cast<std::int64_t>(region.y) + y);
      }
    }
    return out;
  }

  static Jbig2Bitmap::Operator to_operator(unsigned code) {
    switch (code) {
      case 0:
        return Jbig2Bitmap::Operator::bit_or;
      case 1:
        return Jbig2Bitmap::Operator::bit_and;
      case 2:
        return Jbig2Bitmap::Operator::bit_xor;
      case 3:
        return Jbig2Bitmap::Operator::bit_xnor;
      case 4:
        return Jbig2Bitmap::Operator::replace;
      default:
        throw DecodeFault("a JBIG2 region names the combination operator " + std::to_string(code) +
                          ", which the standard does not define");
    }
  }

 private:
  Jbig2Bitmap::Allowance* allowance_;
  Jbig2Segments::PageInfo info_;
  std::uint32_t height_ = 0;
  Jbig2Bitmap bitmap_;
};

// The state of one decode: the page and what the segments define for later
// segments to refer to.
class Session {
 public:
  Session(platform::Budget& budget, std::string_view object) {
    allowance_.budget = &budget;
    allowance_.object = std::string(object);
  }

  void process(const std::vector<Jbig2Segments::Header>& segments) {
    for (const Jbig2Segments::Header& segment : segments) {
      process_segment(segment);
    }
  }

  [[nodiscard]] Jbig2Decoder::Result finish(platform::Budget& budget, std::string_view object) {
    if (!page_) {
      throw DecodeFault("the JBIG2 data holds no page information segment");
    }
    Jbig2Decoder::Result result;
    const std::uint32_t width = page_->width();
    const std::uint32_t height = page_->height();
    const std::size_t row_bytes = (static_cast<std::size_t>(width) + 7U) / 8U;
    const auto size = platform::checked_mul<std::uint64_t>(row_bytes, height, "the JBIG2 page");
    std::uint64_t used = 0;
    budget.charge(size, used, object);
    result.data.assign(static_cast<std::size_t>(size), std::byte{0});
    // The bits are inverted as pdf.js inverts them: 1 (black) becomes 0.
    const unsigned fill = page_->info().default_pixel != 0 ? 0U : 1U;
    for (std::uint32_t y = 0; y < height; ++y) {
      const auto row = page_->bitmap().row(y);
      std::byte* out = result.data.data() + static_cast<std::size_t>(y) * row_bytes;
      for (std::size_t byte = 0; byte < row_bytes; ++byte) {
        unsigned value = 0;
        for (unsigned bit = 0; bit < 8; ++bit) {
          const std::size_t x = byte * 8U + bit;
          const unsigned pixel = x < width ? row[x] : fill ^ 1U;
          value = (value << 1U) | (pixel ^ 1U);
        }
        out[byte] = static_cast<std::byte>(value);
      }
    }
    result.width = width;
    result.height = height;
    return result;
  }

 private:
  using Header = Jbig2Segments::Header;

  Page& require_page(const Header& segment) {
    if (!page_) {
      throw DecodeFault("the JBIG2 segment " + std::to_string(segment.number) + " (" +
                        Jbig2Segments::type_name(segment.type) +
                        ") comes before any page information segment");
    }
    return *page_;
  }

  Jbig2Regions::BitmapList referred_symbols(const Header& segment) const {
    Jbig2Regions::BitmapList symbols;
    for (const std::uint32_t number : segment.referred_to) {
      const auto found = symbols_.find(number);
      if (found != symbols_.end()) {
        symbols.insert(symbols.end(), found->second.begin(), found->second.end());
      }
    }
    return symbols;
  }

  Jbig2Regions::TableList referred_tables(const Header& segment) const {
    Jbig2Regions::TableList tables;
    for (const std::uint32_t number : segment.referred_to) {
      const auto found = tables_.find(number);
      if (found != tables_.end()) {
        tables.push_back(&found->second);
      }
    }
    return tables;
  }

  // An immediate region is drawn on the page; an intermediate one is kept for
  // the segment that refers to it.
  void deliver(const Header& segment, bool immediate, const Jbig2Segments::RegionInfo& info,
               Jbig2Bitmap bitmap) {
    if (immediate) {
      require_page(segment).draw(info, bitmap);
    } else {
      regions_[segment.number] = std::make_shared<const Jbig2Bitmap>(std::move(bitmap));
    }
  }

  void process_segment(const Header& segment) {
    switch (segment.type) {
      case Jbig2Segments::page_information:
        page_.emplace(allowance_, Jbig2Segments::read_page_information(segment));
        break;
      case Jbig2Segments::end_of_stripe:
        if (page_) {
          page_->ensure_height(
              static_cast<std::uint64_t>(Jbig2Segments::read_end_of_stripe(segment)) + 1U);
        }
        break;
      case Jbig2Segments::end_of_page:
      case Jbig2Segments::end_of_file:
      case Jbig2Segments::profiles:
      case Jbig2Segments::extension:
        break;
      case Jbig2Segments::tables:
        tables_.insert_or_assign(segment.number, Jbig2Huffman::read_tables_segment(segment.data));
        break;
      case Jbig2Segments::symbol_dictionary: {
        const auto dictionary = Jbig2Segments::read_symbol_dictionary(segment);
        symbols_[segment.number] = Jbig2Regions::symbol_dictionary(
            dictionary, referred_symbols(segment), referred_tables(segment), allowance_);
        break;
      }
      case Jbig2Segments::pattern_dictionary: {
        const auto dictionary = Jbig2Segments::read_pattern_dictionary(segment);
        patterns_[segment.number] = Jbig2Regions::pattern_dictionary(dictionary, allowance_);
        break;
      }
      case Jbig2Segments::immediate_generic_region:
      case Jbig2Segments::immediate_lossless_generic_region:
      case Jbig2Segments::intermediate_generic_region: {
        const auto region = Jbig2Segments::read_generic_region(segment);
        deliver(segment, segment.type != Jbig2Segments::intermediate_generic_region, region.info,
                Jbig2Regions::generic_region(region, allowance_));
        break;
      }
      case Jbig2Segments::immediate_text_region:
      case Jbig2Segments::immediate_lossless_text_region:
      case Jbig2Segments::intermediate_text_region: {
        const auto region = Jbig2Segments::read_text_region(segment);
        deliver(segment, segment.type != Jbig2Segments::intermediate_text_region, region.info,
                Jbig2Regions::text_region(region, referred_symbols(segment),
                                          referred_tables(segment), allowance_));
        break;
      }
      case Jbig2Segments::immediate_halftone_region:
      case Jbig2Segments::immediate_lossless_halftone_region:
      case Jbig2Segments::intermediate_halftone_region: {
        const auto region = Jbig2Segments::read_halftone_region(segment);
        Jbig2Regions::BitmapList patterns;
        if (!segment.referred_to.empty()) {
          const auto found = patterns_.find(segment.referred_to.front());
          if (found != patterns_.end()) {
            patterns = found->second;
          }
        }
        deliver(segment, segment.type != Jbig2Segments::intermediate_halftone_region, region.info,
                Jbig2Regions::halftone_region(region, patterns, allowance_));
        break;
      }
      case Jbig2Segments::immediate_refinement_region:
      case Jbig2Segments::immediate_lossless_refinement_region:
      case Jbig2Segments::intermediate_refinement_region: {
        const auto region = Jbig2Segments::read_refinement_region(segment);
        // The reference is the region the segment refers to, or the part of the
        // page the region covers (7.4.7.4).
        std::shared_ptr<const Jbig2Bitmap> reference;
        for (const std::uint32_t number : segment.referred_to) {
          const auto found = regions_.find(number);
          if (found != regions_.end()) {
            reference = found->second;
            break;
          }
        }
        if (!reference) {
          reference = std::make_shared<const Jbig2Bitmap>(require_page(segment).crop(region.info));
        }
        deliver(segment, segment.type != Jbig2Segments::intermediate_refinement_region, region.info,
                Jbig2Regions::refinement_region(region, *reference, allowance_));
        break;
      }
      default:
        throw DecodeFault("the JBIG2 segment " + std::to_string(segment.number) + " has the type " +
                          std::to_string(segment.type) + " (" +
                          Jbig2Segments::type_name(segment.type) +
                          "), which this decoder does not read");
    }
  }

  Jbig2Bitmap::Allowance allowance_;
  std::optional<Page> page_;
  std::map<std::uint32_t, Jbig2Regions::BitmapList> symbols_;
  std::map<std::uint32_t, Jbig2Regions::BitmapList> patterns_;
  std::map<std::uint32_t, Jbig2Huffman::Table> tables_;
  std::map<std::uint32_t, std::shared_ptr<const Jbig2Bitmap>> regions_;
};

}  // namespace

Jbig2Decoder::Result Jbig2Decoder::decode(platform::ByteSpan data, platform::ByteSpan globals,
                                          platform::Budget& budget, std::string_view object) {
  try {
    Session session(budget, object);
    if (!globals.empty()) {
      session.process(Jbig2Segments::read(globals));
    }
    session.process(Jbig2Segments::read(data));
    return session.finish(budget, object);
  } catch (const DecodeFault& fault) {
    Result result;
    result.fault = fault.what();
    return result;
  }
}

}  // namespace pdfkit::images
