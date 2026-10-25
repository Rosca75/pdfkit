// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The reader walks the CFF structures with bounds-checked reads; an offset
// that leaves the data ends that structure. The 391 standard strings and the
// three predefined charsets are ported from pdf.js src/core/cff_parser.js
// (CFFStandardStrings) and src/core/charsets.js (Apache-2.0, read at commit
// e2a5e1d34517b7e544f234d54eec3aa37df0b846), which copy Technical Note 5176's
// appendices A and C. A charstring's width is read with the subroutine bias
// of Technical Note 5177 (107, 1131 or 32768 by the INDEX count) and a call
// depth of 10, pdf.js's MAX_SUBR_NESTING.

#include "fonts/cff_font.hpp"

#include "fonts/encodings.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace pdfkit::fonts {

namespace {

using platform::ByteSpan;
using platform::read_be;

// clang-format off
constexpr std::array<std::string_view, 391> standard_strings = {
    ".notdef", "space", "exclam", "quotedbl", "numbersign", "dollar", "percent", "ampersand",
    "quoteright", "parenleft", "parenright", "asterisk", "plus", "comma", "hyphen", "period",
    "slash", "zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
    "colon", "semicolon", "less", "equal", "greater", "question", "at", "A", "B", "C", "D", "E",
    "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W",
    "X", "Y", "Z", "bracketleft", "backslash", "bracketright", "asciicircum", "underscore",
    "quoteleft", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p",
    "q", "r", "s", "t", "u", "v", "w", "x", "y", "z", "braceleft", "bar", "braceright",
    "asciitilde", "exclamdown", "cent", "sterling", "fraction", "yen", "florin", "section",
    "currency", "quotesingle", "quotedblleft", "guillemotleft", "guilsinglleft",
    "guilsinglright", "fi", "fl", "endash", "dagger", "daggerdbl", "periodcentered", "paragraph",
    "bullet", "quotesinglbase", "quotedblbase", "quotedblright", "guillemotright", "ellipsis",
    "perthousand", "questiondown", "grave", "acute", "circumflex", "tilde", "macron", "breve",
    "dotaccent", "dieresis", "ring", "cedilla", "hungarumlaut", "ogonek", "caron", "emdash",
    "AE", "ordfeminine", "Lslash", "Oslash", "OE", "ordmasculine", "ae", "dotlessi", "lslash",
    "oslash", "oe", "germandbls", "onesuperior", "logicalnot", "mu", "trademark", "Eth",
    "onehalf", "plusminus", "Thorn", "onequarter", "divide", "brokenbar", "degree", "thorn",
    "threequarters", "twosuperior", "registered", "minus", "eth", "multiply", "threesuperior",
    "copyright", "Aacute", "Acircumflex", "Adieresis", "Agrave", "Aring", "Atilde", "Ccedilla",
    "Eacute", "Ecircumflex", "Edieresis", "Egrave", "Iacute", "Icircumflex", "Idieresis",
    "Igrave", "Ntilde", "Oacute", "Ocircumflex", "Odieresis", "Ograve", "Otilde", "Scaron",
    "Uacute", "Ucircumflex", "Udieresis", "Ugrave", "Yacute", "Ydieresis", "Zcaron", "aacute",
    "acircumflex", "adieresis", "agrave", "aring", "atilde", "ccedilla", "eacute", "ecircumflex",
    "edieresis", "egrave", "iacute", "icircumflex", "idieresis", "igrave", "ntilde", "oacute",
    "ocircumflex", "odieresis", "ograve", "otilde", "scaron", "uacute", "ucircumflex",
    "udieresis", "ugrave", "yacute", "ydieresis", "zcaron", "exclamsmall", "Hungarumlautsmall",
    "dollaroldstyle", "dollarsuperior", "ampersandsmall", "Acutesmall", "parenleftsuperior",
    "parenrightsuperior", "twodotenleader", "onedotenleader", "zerooldstyle", "oneoldstyle",
    "twooldstyle", "threeoldstyle", "fouroldstyle", "fiveoldstyle", "sixoldstyle",
    "sevenoldstyle", "eightoldstyle", "nineoldstyle", "commasuperior", "threequartersemdash",
    "periodsuperior", "questionsmall", "asuperior", "bsuperior", "centsuperior", "dsuperior",
    "esuperior", "isuperior", "lsuperior", "msuperior", "nsuperior", "osuperior", "rsuperior",
    "ssuperior", "tsuperior", "ff", "ffi", "ffl", "parenleftinferior", "parenrightinferior",
    "Circumflexsmall", "hyphensuperior", "Gravesmall", "Asmall", "Bsmall", "Csmall", "Dsmall",
    "Esmall", "Fsmall", "Gsmall", "Hsmall", "Ismall", "Jsmall", "Ksmall", "Lsmall", "Msmall",
    "Nsmall", "Osmall", "Psmall", "Qsmall", "Rsmall", "Ssmall", "Tsmall", "Usmall", "Vsmall",
    "Wsmall", "Xsmall", "Ysmall", "Zsmall", "colonmonetary", "onefitted", "rupiah", "Tildesmall",
    "exclamdownsmall", "centoldstyle", "Lslashsmall", "Scaronsmall", "Zcaronsmall",
    "Dieresissmall", "Brevesmall", "Caronsmall", "Dotaccentsmall", "Macronsmall", "figuredash",
    "hypheninferior", "Ogoneksmall", "Ringsmall", "Cedillasmall", "questiondownsmall",
    "oneeighth", "threeeighths", "fiveeighths", "seveneighths", "onethird", "twothirds",
    "zerosuperior", "foursuperior", "fivesuperior", "sixsuperior", "sevensuperior",
    "eightsuperior", "ninesuperior", "zeroinferior", "oneinferior", "twoinferior",
    "threeinferior", "fourinferior", "fiveinferior", "sixinferior", "seveninferior",
    "eightinferior", "nineinferior", "centinferior", "dollarinferior", "periodinferior",
    "commainferior", "Agravesmall", "Aacutesmall", "Acircumflexsmall", "Atildesmall",
    "Adieresissmall", "Aringsmall", "AEsmall", "Ccedillasmall", "Egravesmall", "Eacutesmall",
    "Ecircumflexsmall", "Edieresissmall", "Igravesmall", "Iacutesmall", "Icircumflexsmall",
    "Idieresissmall", "Ethsmall", "Ntildesmall", "Ogravesmall", "Oacutesmall",
    "Ocircumflexsmall", "Otildesmall", "Odieresissmall", "OEsmall", "Oslashsmall", "Ugravesmall",
    "Uacutesmall", "Ucircumflexsmall", "Udieresissmall", "Yacutesmall", "Thornsmall",
    "Ydieresissmall", "001.000", "001.001", "001.002", "001.003", "Black", "Bold", "Book",
    "Light", "Medium", "Regular", "Roman", "Semibold",
};

constexpr std::array<std::string_view, 229> iso_adobe_charset = {
    ".notdef", "space", "exclam", "quotedbl", "numbersign", "dollar", "percent", "ampersand",
    "quoteright", "parenleft", "parenright", "asterisk", "plus", "comma", "hyphen", "period",
    "slash", "zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
    "colon", "semicolon", "less", "equal", "greater", "question", "at", "A", "B", "C", "D", "E",
    "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W",
    "X", "Y", "Z", "bracketleft", "backslash", "bracketright", "asciicircum", "underscore",
    "quoteleft", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p",
    "q", "r", "s", "t", "u", "v", "w", "x", "y", "z", "braceleft", "bar", "braceright",
    "asciitilde", "exclamdown", "cent", "sterling", "fraction", "yen", "florin", "section",
    "currency", "quotesingle", "quotedblleft", "guillemotleft", "guilsinglleft",
    "guilsinglright", "fi", "fl", "endash", "dagger", "daggerdbl", "periodcentered", "paragraph",
    "bullet", "quotesinglbase", "quotedblbase", "quotedblright", "guillemotright", "ellipsis",
    "perthousand", "questiondown", "grave", "acute", "circumflex", "tilde", "macron", "breve",
    "dotaccent", "dieresis", "ring", "cedilla", "hungarumlaut", "ogonek", "caron", "emdash",
    "AE", "ordfeminine", "Lslash", "Oslash", "OE", "ordmasculine", "ae", "dotlessi", "lslash",
    "oslash", "oe", "germandbls", "onesuperior", "logicalnot", "mu", "trademark", "Eth",
    "onehalf", "plusminus", "Thorn", "onequarter", "divide", "brokenbar", "degree", "thorn",
    "threequarters", "twosuperior", "registered", "minus", "eth", "multiply", "threesuperior",
    "copyright", "Aacute", "Acircumflex", "Adieresis", "Agrave", "Aring", "Atilde", "Ccedilla",
    "Eacute", "Ecircumflex", "Edieresis", "Egrave", "Iacute", "Icircumflex", "Idieresis",
    "Igrave", "Ntilde", "Oacute", "Ocircumflex", "Odieresis", "Ograve", "Otilde", "Scaron",
    "Uacute", "Ucircumflex", "Udieresis", "Ugrave", "Yacute", "Ydieresis", "Zcaron", "aacute",
    "acircumflex", "adieresis", "agrave", "aring", "atilde", "ccedilla", "eacute", "ecircumflex",
    "edieresis", "egrave", "iacute", "icircumflex", "idieresis", "igrave", "ntilde", "oacute",
    "ocircumflex", "odieresis", "ograve", "otilde", "scaron", "uacute", "ucircumflex",
    "udieresis", "ugrave", "yacute", "ydieresis", "zcaron",
};

constexpr std::array<std::string_view, 166> expert_charset = {
    ".notdef", "space", "exclamsmall", "Hungarumlautsmall", "dollaroldstyle", "dollarsuperior",
    "ampersandsmall", "Acutesmall", "parenleftsuperior", "parenrightsuperior", "twodotenleader",
    "onedotenleader", "comma", "hyphen", "period", "fraction", "zerooldstyle", "oneoldstyle",
    "twooldstyle", "threeoldstyle", "fouroldstyle", "fiveoldstyle", "sixoldstyle",
    "sevenoldstyle", "eightoldstyle", "nineoldstyle", "colon", "semicolon", "commasuperior",
    "threequartersemdash", "periodsuperior", "questionsmall", "asuperior", "bsuperior",
    "centsuperior", "dsuperior", "esuperior", "isuperior", "lsuperior", "msuperior", "nsuperior",
    "osuperior", "rsuperior", "ssuperior", "tsuperior", "ff", "fi", "fl", "ffi", "ffl",
    "parenleftinferior", "parenrightinferior", "Circumflexsmall", "hyphensuperior", "Gravesmall",
    "Asmall", "Bsmall", "Csmall", "Dsmall", "Esmall", "Fsmall", "Gsmall", "Hsmall", "Ismall",
    "Jsmall", "Ksmall", "Lsmall", "Msmall", "Nsmall", "Osmall", "Psmall", "Qsmall", "Rsmall",
    "Ssmall", "Tsmall", "Usmall", "Vsmall", "Wsmall", "Xsmall", "Ysmall", "Zsmall",
    "colonmonetary", "onefitted", "rupiah", "Tildesmall", "exclamdownsmall", "centoldstyle",
    "Lslashsmall", "Scaronsmall", "Zcaronsmall", "Dieresissmall", "Brevesmall", "Caronsmall",
    "Dotaccentsmall", "Macronsmall", "figuredash", "hypheninferior", "Ogoneksmall", "Ringsmall",
    "Cedillasmall", "onequarter", "onehalf", "threequarters", "questiondownsmall", "oneeighth",
    "threeeighths", "fiveeighths", "seveneighths", "onethird", "twothirds", "zerosuperior",
    "onesuperior", "twosuperior", "threesuperior", "foursuperior", "fivesuperior", "sixsuperior",
    "sevensuperior", "eightsuperior", "ninesuperior", "zeroinferior", "oneinferior",
    "twoinferior", "threeinferior", "fourinferior", "fiveinferior", "sixinferior",
    "seveninferior", "eightinferior", "nineinferior", "centinferior", "dollarinferior",
    "periodinferior", "commainferior", "Agravesmall", "Aacutesmall", "Acircumflexsmall",
    "Atildesmall", "Adieresissmall", "Aringsmall", "AEsmall", "Ccedillasmall", "Egravesmall",
    "Eacutesmall", "Ecircumflexsmall", "Edieresissmall", "Igravesmall", "Iacutesmall",
    "Icircumflexsmall", "Idieresissmall", "Ethsmall", "Ntildesmall", "Ogravesmall",
    "Oacutesmall", "Ocircumflexsmall", "Otildesmall", "Odieresissmall", "OEsmall", "Oslashsmall",
    "Ugravesmall", "Uacutesmall", "Ucircumflexsmall", "Udieresissmall", "Yacutesmall",
    "Thornsmall", "Ydieresissmall",
};

constexpr std::array<std::string_view, 87> expert_subset_charset = {
    ".notdef", "space", "dollaroldstyle", "dollarsuperior", "parenleftsuperior",
    "parenrightsuperior", "twodotenleader", "onedotenleader", "comma", "hyphen", "period",
    "fraction", "zerooldstyle", "oneoldstyle", "twooldstyle", "threeoldstyle", "fouroldstyle",
    "fiveoldstyle", "sixoldstyle", "sevenoldstyle", "eightoldstyle", "nineoldstyle", "colon",
    "semicolon", "commasuperior", "threequartersemdash", "periodsuperior", "asuperior",
    "bsuperior", "centsuperior", "dsuperior", "esuperior", "isuperior", "lsuperior", "msuperior",
    "nsuperior", "osuperior", "rsuperior", "ssuperior", "tsuperior", "ff", "fi", "fl", "ffi",
    "ffl", "parenleftinferior", "parenrightinferior", "hyphensuperior", "colonmonetary",
    "onefitted", "rupiah", "centoldstyle", "figuredash", "hypheninferior", "onequarter",
    "onehalf", "threequarters", "oneeighth", "threeeighths", "fiveeighths", "seveneighths",
    "onethird", "twothirds", "zerosuperior", "onesuperior", "twosuperior", "threesuperior",
    "foursuperior", "fivesuperior", "sixsuperior", "sevensuperior", "eightsuperior",
    "ninesuperior", "zeroinferior", "oneinferior", "twoinferior", "threeinferior",
    "fourinferior", "fiveinferior", "sixinferior", "seveninferior", "eightinferior",
    "nineinferior", "centinferior", "dollarinferior", "periodinferior", "commainferior",
};

// clang-format on

// The operators of a DICT: a one-byte operator is its value, an escaped one
// 1200 plus its second byte.
constexpr std::uint16_t op_charset = 15;
constexpr std::uint16_t op_encoding = 16;
constexpr std::uint16_t op_charstrings = 17;
constexpr std::uint16_t op_private = 18;
constexpr std::uint16_t op_subrs = 19;
constexpr std::uint16_t op_default_width = 20;
constexpr std::uint16_t op_nominal_width = 21;
constexpr std::uint16_t op_font_bbox = 5;
constexpr std::uint16_t op_font_matrix = 1207;
constexpr std::uint16_t op_ros = 1230;
constexpr std::uint16_t op_fd_array = 1236;
constexpr std::uint16_t op_fd_select = 1237;

// A charstring calls subroutines at most this deep (pdf.js MAX_SUBR_NESTING).
constexpr int max_subr_depth = 10;
// The Type 2 argument stack holds at most 48 operands (Technical Note 5177).
constexpr std::size_t max_stack = 48;

using Dict = std::map<std::uint16_t, std::vector<double>>;

std::uint8_t byte_at(ByteSpan data, std::size_t at) noexcept {
  return at < data.size() ? std::to_integer<std::uint8_t>(data[at]) : 0;
}

// A DICT real: nibbles of digits, '.', 'E', 'E-' and '-', ended by 0xf.
double read_real(ByteSpan data, std::size_t& at) {
  std::string text;
  bool end = false;
  while (!end && at < data.size()) {
    const std::uint8_t value = byte_at(data, at++);
    for (const std::uint8_t nibble :
         {static_cast<std::uint8_t>(value >> 4U), static_cast<std::uint8_t>(value & 0x0FU)}) {
      if (nibble <= 9) {
        text.push_back(static_cast<char>('0' + nibble));
      } else if (nibble == 0x0A) {
        text.push_back('.');
      } else if (nibble == 0x0B) {
        text.push_back('E');
      } else if (nibble == 0x0C) {
        text += "E-";
      } else if (nibble == 0x0E) {
        text.push_back('-');
      } else if (nibble == 0x0F) {
        end = true;
        break;
      }
    }
  }
  double result = 0.0;
  // A real is short and locale-free here; strtod is avoided for its locale.
  double sign = 1.0;
  std::size_t i = 0;
  if (i < text.size() && text[i] == '-') {
    sign = -1.0;
    ++i;
  }
  double fraction = 0.0;
  for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) {
    result = result * 10.0 + (text[i] - '0');
  }
  if (i < text.size() && text[i] == '.') {
    double place = 0.1;
    for (++i; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) {
      fraction += (text[i] - '0') * place;
      place /= 10.0;
    }
  }
  result += fraction;
  if (i < text.size() && text[i] == 'E') {
    ++i;
    double exponent_sign = 1.0;
    if (i < text.size() && text[i] == '-') {
      exponent_sign = -1.0;
      ++i;
    }
    int exponent = 0;
    for (; i < text.size() && text[i] >= '0' && text[i] <= '9' && exponent < 400; ++i) {
      exponent = exponent * 10 + (text[i] - '0');
    }
    result *= std::pow(10.0, exponent_sign * exponent);
  }
  return sign * result;
}

Dict read_dict(ByteSpan data) {
  Dict dict;
  std::vector<double> operands;
  std::size_t at = 0;
  while (at < data.size()) {
    const std::uint8_t b0 = byte_at(data, at);
    if (b0 <= 21) {
      std::uint16_t op = b0;
      ++at;
      if (b0 == 12) {
        op = static_cast<std::uint16_t>(1200 + byte_at(data, at));
        ++at;
      }
      dict[op] = operands;
      operands.clear();
    } else if (b0 == 28) {
      operands.push_back(
          static_cast<std::int16_t>(read_be<std::uint16_t>(data, at + 1).value_or(0)));
      at += 3;
    } else if (b0 == 29) {
      operands.push_back(
          static_cast<std::int32_t>(read_be<std::uint32_t>(data, at + 1).value_or(0)));
      at += 5;
    } else if (b0 == 30) {
      ++at;
      operands.push_back(read_real(data, at));
    } else if (b0 >= 32 && b0 <= 246) {
      operands.push_back(static_cast<double>(b0) - 139.0);
      ++at;
    } else if (b0 >= 247 && b0 <= 250) {
      operands.push_back((b0 - 247) * 256.0 + byte_at(data, at + 1) + 108.0);
      at += 2;
    } else if (b0 >= 251 && b0 <= 254) {
      operands.push_back(-(b0 - 251) * 256.0 - byte_at(data, at + 1) - 108.0);
      at += 2;
    } else {
      // 22 to 27 and 31 are reserved, 255 is not a DICT operand: skip it.
      ++at;
    }
    if (operands.size() > max_stack) {
      break;
    }
  }
  return dict;
}

// The operand i of key, or no value.
std::optional<double> operand(const Dict& dict, std::uint16_t key, std::size_t i = 0) {
  const auto found = dict.find(key);
  if (found == dict.end() || i >= found->second.size()) {
    return std::nullopt;
  }
  return found->second[i];
}

// A non-negative offset or size operand that fits in size_t.
std::optional<std::size_t> offset_operand(const Dict& dict, std::uint16_t key, std::size_t i = 0) {
  const std::optional<double> value = operand(dict, key, i);
  if (!value || !std::isfinite(*value) || *value < 0 || *value > 2147483647.0) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(*value);
}

double bias_of(std::size_t count) noexcept {
  if (count < 1240) {
    return 107.0;
  }
  if (count < 33900) {
    return 1131.0;
  }
  return 32768.0;
}

}  // namespace

std::optional<CffFont::Index> CffFont::read_index(ByteSpan data, std::size_t& at,
                                                  platform::Budget& budget) {
  const std::optional<std::uint16_t> count = read_be<std::uint16_t>(data, at);
  if (!count) {
    return std::nullopt;
  }
  Index index;
  if (*count == 0) {
    at += 2;
    index.data_start = at;
    return index;
  }
  const std::uint8_t off_size = byte_at(data, at + 2);
  if (off_size < 1 || off_size > 4) {
    return std::nullopt;
  }
  const std::size_t table = at + 3;
  const std::size_t entries = std::size_t{*count} + 1;
  if (table + entries * off_size > data.size()) {
    return std::nullopt;
  }
  budget.check_claim(entries * sizeof(std::uint32_t), "a CFF INDEX of a font program");
  index.offsets.reserve(entries);
  for (std::size_t i = 0; i < entries; ++i) {
    std::uint32_t value = 0;
    for (std::size_t k = 0; k < off_size; ++k) {
      value = (value << 8U) | byte_at(data, table + i * off_size + k);
    }
    // Offsets are 1-based and never decrease.
    if (value == 0 || (!index.offsets.empty() && value - 1 < index.offsets.back())) {
      return std::nullopt;
    }
    index.offsets.push_back(value - 1);
  }
  index.data_start = table + entries * off_size;
  if (index.data_start + index.offsets.back() > data.size()) {
    return std::nullopt;
  }
  at = index.data_start + index.offsets.back();
  return index;
}

ByteSpan CffFont::object(const Index& index, std::size_t i) const noexcept {
  if (i >= index.count()) {
    return {};
  }
  const std::size_t start = index.data_start + index.offsets[i];
  const std::size_t end = index.data_start + index.offsets[i + 1];
  if (end > bytes().size() || start > end) {
    return {};
  }
  return bytes().subspan(start, end - start);
}

std::string CffFont::string_of(std::uint32_t sid) const {
  if (sid < standard_strings.size()) {
    return std::string(standard_strings[sid]);
  }
  const ByteSpan text = object(strings_, sid - standard_strings.size());
  std::string out;
  out.reserve(text.size());
  for (const std::byte value : text) {
    out.push_back(static_cast<char>(value));
  }
  return out;
}

std::optional<CffFont> CffFont::parse(ByteSpan data, platform::Budget& budget) {
  if (data.size() < 4 || byte_at(data, 0) != 1) {
    return std::nullopt;
  }
  CffFont font;
  budget.check_claim(data.size(), "a CFF font program");
  font.data_ = std::make_shared<const std::vector<std::byte>>(data.begin(), data.end());
  const ByteSpan bytes = font.bytes();
  std::size_t at = byte_at(bytes, 2);
  const std::optional<Index> names = read_index(bytes, at, budget);
  const std::optional<Index> top = names ? read_index(bytes, at, budget) : std::nullopt;
  if (!top || top->count() == 0) {
    return std::nullopt;
  }
  if (std::optional<Index> strings = read_index(bytes, at, budget)) {
    font.strings_ = std::move(*strings);
    if (std::optional<Index> subrs = read_index(bytes, at, budget)) {
      font.global_subrs_ = std::move(*subrs);
    }
  }
  const Dict top_dict = read_dict(font.object(*top, 0));
  std::size_t charstrings_at = offset_operand(top_dict, op_charstrings).value_or(0);
  if (charstrings_at == 0) {
    return std::nullopt;
  }
  std::optional<Index> charstrings = read_index(bytes, charstrings_at, budget);
  if (!charstrings) {
    return std::nullopt;
  }
  font.charstrings_ = std::move(*charstrings);
  font.glyph_count_ = static_cast<std::uint32_t>(font.charstrings_.count());
  font.cid_keyed_ = top_dict.contains(op_ros);
  const std::optional<double> top_scale = operand(top_dict, op_font_matrix);
  if (top_scale && std::isfinite(*top_scale) && *top_scale != 0.0) {
    font.scale_ = *top_scale;
  }
  for (std::size_t i = 0; i < 4; ++i) {
    font.bbox_[i] = operand(top_dict, op_font_bbox, i).value_or(0.0);
  }

  if (font.cid_keyed_) {
    std::size_t fd_at = offset_operand(top_dict, op_fd_array).value_or(0);
    const std::optional<Index> fds = fd_at != 0 ? read_index(bytes, fd_at, budget) : std::nullopt;
    for (std::size_t i = 0; fds && i < fds->count() && i < 256; ++i) {
      const Dict fd = read_dict(font.object(*fds, i));
      double scale = font.scale_;
      const std::optional<double> fd_scale = operand(fd, op_font_matrix);
      if (fd_scale && std::isfinite(*fd_scale) && *fd_scale != 0.0) {
        scale = top_scale ? *top_scale * *fd_scale : *fd_scale;
      }
      const std::size_t size = offset_operand(fd, op_private, 0).value_or(0);
      const std::size_t offset = offset_operand(fd, op_private, 1).value_or(0);
      const ByteSpan priv = offset < bytes.size()
                                ? bytes.subspan(offset, std::min(size, bytes.size() - offset))
                                : ByteSpan();
      Private value = font.read_private(priv, scale, budget);
      if (!priv.empty()) {
        if (const std::optional<std::size_t> subrs = offset_operand(read_dict(priv), op_subrs)) {
          std::size_t subrs_at = offset + *subrs;
          if (std::optional<Index> index = read_index(bytes, subrs_at, budget)) {
            value.subrs = std::move(*index);
          }
        }
      }
      font.privates_.push_back(std::move(value));
    }
    if (const std::optional<std::size_t> select = offset_operand(top_dict, op_fd_select)) {
      font.read_fd_select(*select, budget);
    }
  } else {
    const std::size_t size = offset_operand(top_dict, op_private, 0).value_or(0);
    const std::size_t offset = offset_operand(top_dict, op_private, 1).value_or(0);
    const ByteSpan priv = offset < bytes.size() && size > 0
                              ? bytes.subspan(offset, std::min(size, bytes.size() - offset))
                              : ByteSpan();
    Private value = font.read_private(priv, font.scale_, budget);
    if (!priv.empty()) {
      if (const std::optional<std::size_t> subrs = offset_operand(read_dict(priv), op_subrs)) {
        std::size_t subrs_at = offset + *subrs;
        if (std::optional<Index> index = read_index(bytes, subrs_at, budget)) {
          value.subrs = std::move(*index);
        }
      }
    }
    font.privates_.push_back(std::move(value));
  }
  font.read_charset(offset_operand(top_dict, op_charset).value_or(0), budget);
  if (!font.cid_keyed_) {
    font.read_encoding(offset_operand(top_dict, op_encoding).value_or(0));
  }
  return font;
}

CffFont::Private CffFont::read_private(ByteSpan dict, double scale,
                                       platform::Budget& /*budget*/) const {
  Private value;
  value.scale = scale;
  if (dict.empty()) {
    return value;
  }
  const Dict entries = read_dict(dict);
  value.default_width = operand(entries, op_default_width).value_or(0.0);
  value.nominal_width = operand(entries, op_nominal_width).value_or(0.0);
  return value;
}

void CffFont::read_charset(std::size_t offset, platform::Budget& budget) {
  const ByteSpan data = bytes();
  budget.check_claim(std::size_t{glyph_count_} * sizeof(std::uint32_t),
                     "the charset of a CFF font program");
  std::vector<std::uint32_t> sids;
  sids.reserve(glyph_count_);
  if (glyph_count_ > 0) {
    sids.push_back(0);
  }
  std::span<const std::string_view> predefined;
  if (offset == 0) {
    predefined = iso_adobe_charset;
  } else if (offset == 1) {
    predefined = expert_charset;
  } else if (offset == 2) {
    predefined = expert_subset_charset;
  } else {
    const std::uint8_t format = byte_at(data, offset);
    std::size_t at = offset + 1;
    while (sids.size() < glyph_count_ && at < data.size()) {
      if (format == 0) {
        sids.push_back(read_be<std::uint16_t>(data, at).value_or(0));
        at += 2;
      } else if (format == 1 || format == 2) {
        // A range: the first SID, then how many follow it (one byte in
        // format 1, two in format 2).
        const std::optional<std::uint16_t> first = read_be<std::uint16_t>(data, at);
        if (!first) {
          break;
        }
        const std::uint32_t count = format == 1 ? std::uint32_t{byte_at(data, at + 2)}
                                                : read_be<std::uint16_t>(data, at + 2).value_or(0);
        at += format == 1 ? 3 : 4;
        for (std::uint32_t k = 0; k <= count && sids.size() < glyph_count_; ++k) {
          sids.push_back(*first + k);
        }
      } else {
        break;
      }
    }
  }
  if (cid_keyed_) {
    cids_ = sids;
    for (std::size_t glyph = 0; glyph < cids_.size(); ++glyph) {
      by_cid_.emplace(cids_[glyph], static_cast<std::uint32_t>(glyph));
    }
    return;
  }
  names_.reserve(glyph_count_);
  if (!predefined.empty()) {
    for (std::size_t glyph = 0; glyph < glyph_count_ && glyph < predefined.size(); ++glyph) {
      names_.emplace_back(predefined[glyph]);
    }
  } else {
    for (const std::uint32_t sid : sids) {
      names_.push_back(string_of(sid));
    }
  }
  for (std::size_t glyph = 0; glyph < names_.size(); ++glyph) {
    by_name_.emplace(names_[glyph], static_cast<std::uint32_t>(glyph));
  }
}

void CffFont::read_encoding(std::size_t offset) {
  if (offset == 0) {
    return;
  }
  standard_encoding_ = false;
  if (offset == 1) {
    expert_encoding_ = true;
    return;
  }
  const ByteSpan data = bytes();
  const std::uint8_t format = byte_at(data, offset);
  std::size_t at = offset + 1;
  std::uint32_t glyph = 1;
  if ((format & 0x7FU) == 0) {
    const std::uint8_t count = byte_at(data, at++);
    for (std::uint8_t i = 0; i < count && at < data.size(); ++i) {
      encoding_.emplace(byte_at(data, at++), glyph++);
    }
  } else if ((format & 0x7FU) == 1) {
    const std::uint8_t ranges = byte_at(data, at++);
    for (std::uint8_t i = 0; i < ranges && at + 1 < data.size(); ++i) {
      const std::uint32_t first = byte_at(data, at);
      const std::uint32_t left = byte_at(data, at + 1);
      at += 2;
      for (std::uint32_t k = 0; k <= left && first + k < 256; ++k) {
        encoding_.emplace(static_cast<std::uint8_t>(first + k), glyph++);
      }
    }
  } else {
    return;
  }
  if ((format & 0x80U) != 0) {
    const std::uint8_t supplements = byte_at(data, at++);
    for (std::uint8_t i = 0; i < supplements && at + 2 < data.size(); ++i) {
      const std::uint8_t code = byte_at(data, at);
      const std::uint16_t sid = read_be<std::uint16_t>(data, at + 1).value_or(0);
      at += 3;
      if (const std::optional<std::uint32_t> target = glyph_by_name(string_of(sid))) {
        encoding_[code] = *target;
      }
    }
  }
}

void CffFont::read_fd_select(std::size_t offset, platform::Budget& budget) {
  const ByteSpan data = bytes();
  budget.check_claim(glyph_count_, "the FDSelect of a CFF font program");
  fd_select_.assign(glyph_count_, 0);
  const std::uint8_t format = byte_at(data, offset);
  if (format == 0) {
    for (std::uint32_t glyph = 0; glyph < glyph_count_; ++glyph) {
      fd_select_[glyph] = byte_at(data, offset + 1 + glyph);
    }
  } else if (format == 3) {
    const std::uint16_t ranges = read_be<std::uint16_t>(data, offset + 1).value_or(0);
    for (std::uint16_t i = 0; i < ranges; ++i) {
      const std::size_t at = offset + 3 + std::size_t{i} * 3;
      const std::uint16_t first = read_be<std::uint16_t>(data, at).value_or(0);
      const std::uint8_t fd = byte_at(data, at + 2);
      const std::uint16_t next = read_be<std::uint16_t>(data, at + 3).value_or(0);
      for (std::uint32_t glyph = first; glyph < next && glyph < glyph_count_; ++glyph) {
        fd_select_[glyph] = fd;
      }
    }
  }
}

std::string_view CffFont::glyph_name(std::uint32_t glyph) const noexcept {
  return glyph < names_.size() ? std::string_view(names_[glyph]) : std::string_view();
}

std::optional<std::uint32_t> CffFont::glyph_by_name(std::string_view name) const {
  const auto found = by_name_.find(name);
  if (found == by_name_.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::optional<std::uint32_t> CffFont::glyph_for_cid(std::uint32_t cid) const {
  if (!cid_keyed_) {
    if (cid < glyph_count_) {
      return cid;
    }
    return std::nullopt;
  }
  if (cids_.empty()) {
    return cid < glyph_count_ ? std::optional<std::uint32_t>(cid) : std::nullopt;
  }
  const auto found = by_cid_.find(cid);
  if (found == by_cid_.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::optional<std::uint32_t> CffFont::glyph_for_code(std::uint8_t code) const {
  if (!encoding_.empty() || (!standard_encoding_ && !expert_encoding_)) {
    const auto found = encoding_.find(code);
    if (found == encoding_.end()) {
      return std::nullopt;
    }
    return found->second;
  }
  const std::string_view name = Encodings::name(
      expert_encoding_ ? Encodings::Base::cff_expert : Encodings::Base::standard, code);
  if (name.empty()) {
    return std::nullopt;
  }
  return glyph_by_name(name);
}

std::optional<double> CffFont::width(std::uint32_t glyph) const {
  if (glyph >= glyph_count_ || privates_.empty()) {
    return std::nullopt;
  }
  std::size_t fd = 0;
  if (cid_keyed_ && glyph < fd_select_.size()) {
    fd = fd_select_[glyph];
  }
  if (fd >= privates_.size()) {
    fd = 0;
  }
  const Private& priv = privates_[fd];
  const std::optional<double> value = charstring_width(object(charstrings_, glyph), priv);
  return value.value_or(priv.default_width) * priv.scale * 1000.0;
}

std::optional<double> CffFont::charstring_width(ByteSpan charstring, const Private& priv) const {
  std::vector<double> stack;
  std::optional<double> found;
  bool done = false;
  // Runs one charstring or subroutine; true when the width question is
  // settled.
  const auto run = [&](const auto& self, ByteSpan code, int depth) -> void {
    std::size_t at = 0;
    while (at < code.size() && !done) {
      const std::uint8_t b0 = byte_at(code, at);
      if (b0 == 28) {
        stack.push_back(
            static_cast<std::int16_t>(read_be<std::uint16_t>(code, at + 1).value_or(0)));
        at += 3;
      } else if (b0 >= 32 && b0 <= 246) {
        stack.push_back(static_cast<double>(b0) - 139.0);
        at += 1;
      } else if (b0 >= 247 && b0 <= 250) {
        stack.push_back((b0 - 247) * 256.0 + byte_at(code, at + 1) + 108.0);
        at += 2;
      } else if (b0 >= 251 && b0 <= 254) {
        stack.push_back(-(b0 - 251) * 256.0 - byte_at(code, at + 1) - 108.0);
        at += 2;
      } else if (b0 == 255) {
        stack.push_back(
            static_cast<std::int32_t>(read_be<std::uint32_t>(code, at + 1).value_or(0)) / 65536.0);
        at += 5;
      } else {
        at += 1;
        switch (b0) {
          case 1:   // hstem
          case 3:   // vstem
          case 18:  // hstemhm
          case 23:  // vstemhm
          case 19:  // hintmask
          case 20:  // cntrmask
            if (stack.size() % 2 == 1) {
              found = stack.front();
            }
            done = true;
            break;
          case 21:  // rmoveto
            if (stack.size() > 2) {
              found = stack.front();
            }
            done = true;
            break;
          case 4:   // vmoveto
          case 22:  // hmoveto
            if (stack.size() > 1) {
              found = stack.front();
            }
            done = true;
            break;
          case 14:  // endchar
            if (stack.size() == 1 || stack.size() == 5) {
              found = stack.front();
            }
            done = true;
            break;
          case 10:    // callsubr
          case 29: {  // callgsubr
            if (stack.empty() || depth >= max_subr_depth) {
              done = true;
              break;
            }
            const Index& subrs = b0 == 10 ? priv.subrs : global_subrs_;
            const double index = stack.back() + bias_of(subrs.count());
            stack.pop_back();
            if (index < 0 || index >= static_cast<double>(subrs.count())) {
              done = true;
              break;
            }
            self(self, object(subrs, static_cast<std::size_t>(index)), depth + 1);
            break;
          }
          case 11:  // return
            return;
          default:
            // Any other operator before the first stack-clearing one leaves
            // the width at its default.
            done = true;
            break;
        }
      }
      if (stack.size() > max_stack) {
        done = true;
      }
    }
  };
  run(run, charstring, 0);
  if (!found) {
    return std::nullopt;
  }
  return priv.nominal_width + *found;
}

}  // namespace pdfkit::fonts
