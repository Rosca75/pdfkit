// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The four function types of ISO 32000-1 section 7.10 (recalled). Type 0
// interpolates multilinearly over its inputs, as pdf.js's PDFFunction does
// (recalled); a type 4 program is compiled once into a flat instruction list
// with jumps for if and ifelse, so an evaluation is a loop over that list
// bounded by the operator budget.

#include "images/pdf_function.hpp"

#include "images/decode_fault.hpp"
#include "platform/checked.hpp"
#include "syntax/dictionary.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace pdfkit::images {

namespace {

using syntax::Dictionary;
using syntax::Document;
using syntax::Object;

// The calculator operators of ISO 32000-1 table 42 (recalled).
constexpr std::array<std::string_view, 40> operator_names{
    "abs",      "add", "atan",     "ceiling", "cos",   "cvi", "cvr",   "div",   "exp",  "floor",
    "idiv",     "ln",  "log",      "mod",     "mul",   "neg", "round", "sin",   "sqrt", "sub",
    "truncate", "and", "bitshift", "eq",      "false", "ge",  "gt",    "le",    "lt",   "ne",
    "not",      "or",  "true",     "xor",     "copy",  "dup", "exch",  "index", "pop",  "roll"};

[[noreturn]] void fault(const std::string& what) {
  throw DecodeFault("a function " + what);
}

std::vector<double> numbers(Document& document, const Dictionary& dictionary,
                            std::string_view key) {
  std::vector<double> out;
  const Object value = document.resolve_entry(dictionary, key);
  if (const Object::Array* list = value.array()) {
    out.reserve(list->size());
    for (const Object& item : *list) {
      const Object resolved = document.resolve(item);
      const std::optional<double> number = resolved.number();
      if (!number || !std::isfinite(*number)) {
        fault("has a non-number in its /" + std::string(key));
      }
      out.push_back(*number);
    }
  }
  return out;
}

double clip(double value, double low, double high) {
  if (low > high) {
    std::swap(low, high);
  }
  return std::clamp(value, low, high);
}

double interpolate(double x, double x0, double x1, double y0, double y1) {
  if (x1 == x0) {
    return y0;
  }
  return y0 + (x - x0) * (y1 - y0) / (x1 - x0);
}

// The calculator's value: a number or a boolean.
struct Value {
  double number = 0.0;
  bool boolean = false;
};

class Compiler {
 public:
  explicit Compiler(std::string_view text) : text_(text) {}

  std::vector<PdfFunction::Instruction> compile() {
    skip_space();
    if (!take('{')) {
      fault("program does not start with {");
    }
    block();
    return std::move(program_);
  }

 private:
  using Instruction = PdfFunction::Instruction;

  void skip_space() {
    while (position_ < text_.size()) {
      const char c = text_[position_];
      if (c == '%') {
        while (position_ < text_.size() && text_[position_] != '\n' && text_[position_] != '\r') {
          ++position_;
        }
      } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\0') {
        ++position_;
      } else {
        return;
      }
    }
  }

  bool take(char c) {
    if (position_ < text_.size() && text_[position_] == c) {
      ++position_;
      return true;
    }
    return false;
  }

  void push(Instruction instruction) {
    if (program_.size() >= PdfFunction::max_operators) {
      fault("program holds more than " + std::to_string(PdfFunction::max_operators) + " operators");
    }
    program_.push_back(instruction);
  }

  // Compiles up to and including the closing brace of the block opened just
  // before.
  void block() {
    if (++depth_ > 100) {
      fault("program nests its blocks more than 100 deep");
    }
    for (;;) {
      skip_space();
      if (position_ >= text_.size()) {
        fault("program ends inside a block");
      }
      if (take('}')) {
        --depth_;
        return;
      }
      if (take('{')) {
        conditional();
        continue;
      }
      std::size_t end = position_;
      while (end < text_.size() &&
             std::string_view(" \t\r\n\f{}%").find(text_[end]) == std::string_view::npos) {
        ++end;
      }
      const std::string_view token = text_.substr(position_, end - position_);
      position_ = end;
      double value = 0.0;
      const auto [ptr, error] = std::from_chars(token.data(), token.data() + token.size(), value);
      if (error == std::errc() && ptr == token.data() + token.size() && std::isfinite(value)) {
        push({Instruction::Kind::number, value, 0, 0});
        continue;
      }
      const auto* found = std::ranges::find(operator_names, token);
      if (found == operator_names.end()) {
        fault("program names the unknown operator '" + std::string(token.substr(0, 32)) + "'");
      }
      push({Instruction::Kind::op, 0.0, static_cast<std::uint16_t>(found - operator_names.begin()),
            0});
    }
  }

  // "{ a } if" or "{ a } { b } ifelse", the first brace already taken.
  void conditional() {
    const std::size_t jump_false = program_.size();
    push({Instruction::Kind::jump_if_false, 0.0, 0, 0});
    block();
    skip_space();
    if (take('{')) {
      const std::size_t jump_end = program_.size();
      push({Instruction::Kind::jump, 0.0, 0, 0});
      program_[jump_false].target = program_.size();
      block();
      skip_space();
      expect_word("ifelse");
      program_[jump_end].target = program_.size();
    } else {
      expect_word("if");
      program_[jump_false].target = program_.size();
    }
  }

  void expect_word(std::string_view word) {
    if (text_.substr(position_, word.size()) != word) {
      fault("program has a block not followed by " + std::string(word));
    }
    position_ += word.size();
  }

  std::string_view text_;
  std::size_t position_ = 0;
  int depth_ = 0;
  std::vector<Instruction> program_;
};

}  // namespace

std::shared_ptr<const PdfFunction> PdfFunction::parse(Document& document, const Object& object,
                                                      int depth) {
  if (depth > max_depth) {
    fault("nests stitching functions more than " + std::to_string(max_depth) + " deep");
  }
  const Object resolved = document.resolve(object);
  const Dictionary* dictionary = resolved.dictionary_or_stream_dictionary();
  if (dictionary == nullptr) {
    fault("is neither a dictionary nor a stream");
  }
  const std::optional<std::int64_t> type =
      document.resolve_entry(*dictionary, "FunctionType").integer();
  std::shared_ptr<PdfFunction> function(new PdfFunction());
  function->domain_ = numbers(document, *dictionary, "Domain");
  function->range_ = numbers(document, *dictionary, "Range");
  if (function->domain_.empty() || function->domain_.size() % 2 != 0 ||
      function->domain_.size() > 64) {
    fault("has no usable /Domain");
  }
  if (function->range_.size() % 2 != 0 || function->range_.size() > 64) {
    fault("has an odd or oversized /Range");
  }
  function->outputs_ = function->range_.size() / 2;
  const std::size_t m = function->inputs();
  switch (type.value_or(-1)) {
    case 0: {
      const syntax::Stream* stream = resolved.stream();
      if (stream == nullptr || function->outputs_ == 0) {
        fault("of type 0 is not a stream with a /Range");
      }
      function->type_ = Type::sampled;
      const std::vector<double> size = numbers(document, *dictionary, "Size");
      if (size.size() != m) {
        fault("of type 0 has a /Size of the wrong length");
      }
      std::uint64_t count = 1;
      for (const double value : size) {
        if (value < 1 || value > 1e6 || value != std::floor(value)) {
          fault("of type 0 has an unusable /Size entry");
        }
        function->size_.push_back(static_cast<std::uint32_t>(value));
        count = platform::checked_mul<std::uint64_t>(count, static_cast<std::uint64_t>(value),
                                                     "a sampled function's table");
      }
      const std::uint64_t outputs = function->outputs_;
      count = platform::checked_mul<std::uint64_t>(count, outputs, "a sampled function's table");
      const std::optional<std::int64_t> bits =
          document.resolve_entry(*dictionary, "BitsPerSample").integer();
      if (!bits || (*bits != 1 && *bits != 2 && *bits != 4 && *bits != 8 && *bits != 12 &&
                    *bits != 16 && *bits != 24 && *bits != 32)) {
        fault("of type 0 has an unusable /BitsPerSample");
      }
      document.budget().check_claim(
          platform::checked_mul<std::uint64_t>(count, sizeof(double), "a sampled function"),
          "the sample table of a type 0 function");
      function->encode_ = numbers(document, *dictionary, "Encode");
      if (function->encode_.size() != 2 * m) {
        function->encode_.clear();
        for (const std::uint32_t s : function->size_) {
          function->encode_.push_back(0.0);
          function->encode_.push_back(static_cast<double>(s) - 1.0);
        }
      }
      function->decode_ = numbers(document, *dictionary, "Decode");
      if (function->decode_.size() != 2 * function->outputs_) {
        function->decode_ = function->range_;
      }
      const std::vector<std::byte> data = document.decode(*stream).bytes;
      const auto bit_count = static_cast<std::uint64_t>(*bits);
      const double scale = std::ldexp(1.0, static_cast<int>(bit_count)) - 1.0;
      function->samples_.resize(static_cast<std::size_t>(count), 0.0);
      std::uint64_t bit = 0;
      for (double& sample : function->samples_) {
        std::uint64_t value = 0;
        for (std::uint64_t b = 0; b < bit_count; ++b, ++bit) {
          const auto byte = static_cast<std::size_t>(bit / 8);
          const unsigned v = byte < data.size() ? std::to_integer<unsigned>(data[byte]) : 0U;
          value = (value << 1U) | ((v >> (7U - static_cast<unsigned>(bit % 8))) & 1U);
        }
        sample = static_cast<double>(value) / scale;
      }
      break;
    }
    case 2: {
      function->type_ = Type::exponential;
      function->c0_ = numbers(document, *dictionary, "C0");
      function->c1_ = numbers(document, *dictionary, "C1");
      if (function->c0_.empty() && !dictionary->contains("C0")) {
        function->c0_ = {0.0};
      }
      if (function->c1_.empty() && !dictionary->contains("C1")) {
        function->c1_ = {1.0};
      }
      if (function->c0_.size() != function->c1_.size() || function->c0_.empty() || m != 1) {
        fault("of type 2 has /C0 and /C1 of different lengths or more than one input");
      }
      const std::optional<double> n = document.resolve_entry(*dictionary, "N").number();
      if (!n || !std::isfinite(*n)) {
        fault("of type 2 has no /N");
      }
      function->exponent_ = *n;
      if (function->outputs_ == 0) {
        function->outputs_ = function->c0_.size();
      }
      break;
    }
    case 3: {
      function->type_ = Type::stitching;
      if (m != 1) {
        fault("of type 3 has more than one input");
      }
      const Object list = document.resolve_entry(*dictionary, "Functions");
      const Object::Array* items = list.array();
      if (items == nullptr || items->empty() || items->size() > 256) {
        fault("of type 3 has no usable /Functions");
      }
      for (const Object& item : *items) {
        function->functions_.push_back(parse(document, item, depth + 1));
      }
      function->bounds_ = numbers(document, *dictionary, "Bounds");
      function->encode_ = numbers(document, *dictionary, "Encode");
      if (function->bounds_.size() + 1 != items->size() ||
          function->encode_.size() != 2 * items->size()) {
        fault("of type 3 has /Bounds or /Encode of the wrong length");
      }
      if (function->outputs_ == 0) {
        function->outputs_ = function->functions_.front()->outputs();
      }
      break;
    }
    case 4: {
      const syntax::Stream* stream = resolved.stream();
      if (stream == nullptr || function->outputs_ == 0) {
        fault("of type 4 is not a stream with a /Range");
      }
      function->type_ = Type::calculator;
      const std::vector<std::byte> data = document.decode(*stream).bytes;
      const std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
      function->program_ = Compiler(text).compile();
      break;
    }
    default:
      fault("has no known /FunctionType");
  }
  return function;
}

std::shared_ptr<const PdfFunction> PdfFunction::exponential(std::vector<double> c0,
                                                            std::vector<double> c1,
                                                            double exponent) {
  std::shared_ptr<PdfFunction> function(new PdfFunction());
  function->type_ = Type::exponential;
  function->domain_ = {0.0, 1.0};
  function->outputs_ = c0.size();
  function->c0_ = std::move(c0);
  function->c1_ = std::move(c1);
  function->exponent_ = exponent;
  return function;
}

void PdfFunction::evaluate(std::span<const double> in, std::span<double> out) const {
  const std::size_t m = inputs();
  std::array<double, 32> clipped{};
  for (std::size_t i = 0; i < m && i < clipped.size(); ++i) {
    const double value = i < in.size() && std::isfinite(in[i]) ? in[i] : domain_[2 * i];
    clipped[i] = clip(value, domain_[2 * i], domain_[2 * i + 1]);
  }
  const std::span<const double> x(clipped.data(), std::min(m, clipped.size()));
  std::ranges::fill(out, 0.0);
  switch (type_) {
    case Type::sampled:
      evaluate_sampled(x, out);
      break;
    case Type::exponential:
      for (std::size_t j = 0; j < out.size() && j < c0_.size(); ++j) {
        out[j] = c0_[j] + std::pow(x[0], exponent_) * (c1_[j] - c0_[j]);
      }
      break;
    case Type::stitching:
      evaluate_stitching(x, out);
      break;
    case Type::calculator:
      evaluate_calculator(x, out);
      break;
  }
  for (std::size_t j = 0; j < out.size(); ++j) {
    if (!std::isfinite(out[j])) {
      out[j] = 0.0;
    }
    if (2 * j + 1 < range_.size()) {
      out[j] = clip(out[j], range_[2 * j], range_[2 * j + 1]);
    }
  }
}

void PdfFunction::evaluate_sampled(std::span<const double> in, std::span<double> out) const {
  const std::size_t m = in.size();
  const std::size_t n = outputs_;
  // Per input: the lower sample index, the weight of the upper one, and the
  // stride of that input in the table.
  std::array<std::size_t, 32> low{};
  std::array<double, 32> weight{};
  std::array<std::size_t, 32> stride{};
  std::size_t step = n;
  for (std::size_t i = 0; i < m; ++i) {
    const double last = static_cast<double>(size_[i]) - 1.0;
    double e =
        interpolate(in[i], domain_[2 * i], domain_[2 * i + 1], encode_[2 * i], encode_[2 * i + 1]);
    e = std::clamp(e, 0.0, last);
    const double floor = std::floor(e);
    low[i] = static_cast<std::size_t>(floor);
    weight[i] = e - floor;
    if (low[i] >= size_[i] - 1U) {
      low[i] = size_[i] - 1U;
      weight[i] = 0.0;
    }
    stride[i] = step;
    step *= size_[i];
  }
  // The 2^m corners around the point, each weighted by its distance; inputs
  // whose weight is zero contribute one corner only.
  std::size_t active = 0;
  std::array<std::size_t, 32> which{};
  for (std::size_t i = 0; i < m; ++i) {
    if (weight[i] > 0.0) {
      which[active++] = i;
    }
  }
  if (active > 12) {
    active = 12;
  }
  std::size_t base = 0;
  for (std::size_t i = 0; i < m; ++i) {
    base += low[i] * stride[i];
  }
  for (std::size_t j = 0; j < n; ++j) {
    double sum = 0.0;
    for (std::size_t corner = 0; corner < (std::size_t{1} << active); ++corner) {
      double w = 1.0;
      std::size_t index = base + j;
      for (std::size_t a = 0; a < active; ++a) {
        const std::size_t i = which[a];
        if ((corner >> a) & 1U) {
          w *= weight[i];
          index += stride[i];
        } else {
          w *= 1.0 - weight[i];
        }
      }
      if (index < samples_.size()) {
        sum += w * samples_[index];
      }
    }
    out[j] = decode_[2 * j] + sum * (decode_[2 * j + 1] - decode_[2 * j]);
  }
}

void PdfFunction::evaluate_stitching(std::span<const double> in, std::span<double> out) const {
  const double x = in[0];
  std::size_t k = 0;
  while (k < bounds_.size() && x >= bounds_[k]) {
    ++k;
  }
  const double low = k == 0 ? domain_[0] : bounds_[k - 1];
  const double high = k == bounds_.size() ? domain_[1] : bounds_[k];
  const double e = interpolate(x, low, high, encode_[2 * k], encode_[2 * k + 1]);
  const std::array<double, 1> argument{e};
  std::array<double, 32> result{};
  const std::size_t count = std::min(functions_[k]->outputs(), result.size());
  functions_[k]->evaluate(argument, std::span<double>(result.data(), count));
  for (std::size_t j = 0; j < out.size() && j < count; ++j) {
    out[j] = result[j];
  }
}

void PdfFunction::evaluate_calculator(std::span<const double> in, std::span<double> out) const {
  std::array<Value, max_stack> stack{};
  std::size_t top = 0;
  const auto push = [&](Value value) {
    if (top >= max_stack) {
      fault("program overflows its operand stack of " + std::to_string(max_stack));
    }
    stack[top++] = value;
  };
  const auto pop = [&]() -> Value {
    if (top == 0) {
      fault("program pops an empty operand stack");
    }
    return stack[--top];
  };
  const auto pop_int = [&]() -> std::int64_t {
    const double value = pop().number;
    if (!std::isfinite(value) || std::fabs(value) > 9e15) {
      fault("program needs an integer it does not have");
    }
    return static_cast<std::int64_t>(value);
  };
  for (const double value : in) {
    push({value, false});
  }
  std::size_t executed = 0;
  std::size_t pc = 0;
  while (pc < program_.size()) {
    if (++executed > max_operators) {
      fault("program runs more than " + std::to_string(max_operators) + " operators");
    }
    const Instruction& instruction = program_[pc++];
    switch (instruction.kind) {
      case Instruction::Kind::number:
        push({instruction.value, false});
        continue;
      case Instruction::Kind::jump:
        pc = instruction.target;
        continue;
      case Instruction::Kind::jump_if_false:
        if (pop().number == 0.0) {
          pc = instruction.target;
        }
        continue;
      case Instruction::Kind::op:
        break;
    }
    const std::string_view name = operator_names[instruction.op];
    if (name == "abs") {
      push({std::fabs(pop().number), false});
    } else if (name == "add") {
      const double b = pop().number;
      push({pop().number + b, false});
    } else if (name == "atan") {
      const double den = pop().number;
      const double num = pop().number;
      double angle = std::atan2(num, den) * 180.0 / std::numbers::pi;
      if (angle < 0) {
        angle += 360.0;
      }
      push({angle, false});
    } else if (name == "ceiling") {
      push({std::ceil(pop().number), false});
    } else if (name == "cos") {
      push({std::cos(pop().number * std::numbers::pi / 180.0), false});
    } else if (name == "cvi" || name == "truncate") {
      push({std::trunc(pop().number), false});
    } else if (name == "cvr") {
      push({pop().number, false});
    } else if (name == "div") {
      const double b = pop().number;
      const double a = pop().number;
      push({b == 0.0 ? 0.0 : a / b, false});
    } else if (name == "exp") {
      const double e = pop().number;
      push({std::pow(pop().number, e), false});
    } else if (name == "floor") {
      push({std::floor(pop().number), false});
    } else if (name == "idiv") {
      const std::int64_t b = pop_int();
      const std::int64_t a = pop_int();
      // An integer quotient, as idiv defines it.
      const std::int64_t quotient = b == 0 ? 0 : a / b;
      push({static_cast<double>(quotient), false});
    } else if (name == "ln") {
      push({std::log(pop().number), false});
    } else if (name == "log") {
      push({std::log10(pop().number), false});
    } else if (name == "mod") {
      const std::int64_t b = pop_int();
      const std::int64_t a = pop_int();
      push({b == 0 ? 0.0 : static_cast<double>(a % b), false});
    } else if (name == "mul") {
      const double b = pop().number;
      push({pop().number * b, false});
    } else if (name == "neg") {
      push({-pop().number, false});
    } else if (name == "round") {
      push({std::floor(pop().number + 0.5), false});
    } else if (name == "sin") {
      push({std::sin(pop().number * std::numbers::pi / 180.0), false});
    } else if (name == "sqrt") {
      push({std::sqrt(pop().number), false});
    } else if (name == "sub") {
      const double b = pop().number;
      push({pop().number - b, false});
    } else if (name == "and" || name == "or" || name == "xor") {
      const Value b = pop();
      const Value a = pop();
      if (a.boolean && b.boolean) {
        const bool x = a.number != 0.0;
        const bool y = b.number != 0.0;
        const bool r = name == "and" ? (x && y) : name == "or" ? (x || y) : (x != y);
        push({r ? 1.0 : 0.0, true});
      } else {
        // The bitwise operators work on the two's complement of 32-bit
        // integers, as PostScript's do.
        const auto x = static_cast<std::uint32_t>(static_cast<std::int64_t>(a.number));
        const auto y = static_cast<std::uint32_t>(static_cast<std::int64_t>(b.number));
        const std::uint32_t r = name == "and" ? (x & y) : name == "or" ? (x | y) : (x ^ y);
        push({static_cast<double>(static_cast<std::int32_t>(r)), false});
      }
    } else if (name == "bitshift") {
      const std::int64_t shift = pop_int();
      const std::int64_t value = pop_int();
      const auto bits = static_cast<std::uint32_t>(static_cast<std::uint64_t>(value));
      std::uint32_t r = 0;
      if (shift > 0 && shift < 32) {
        r = bits << static_cast<unsigned>(shift);
      } else if (shift < 0 && shift > -32) {
        r = bits >> static_cast<unsigned>(-shift);
      } else if (shift == 0) {
        r = bits;
      }
      push({static_cast<double>(static_cast<std::int32_t>(r)), false});
    } else if (name == "eq" || name == "ne" || name == "ge" || name == "gt" || name == "le" ||
               name == "lt") {
      const double b = pop().number;
      const double a = pop().number;
      bool r = false;
      if (name == "eq") {
        r = a == b;
      } else if (name == "ne") {
        r = a != b;
      } else if (name == "ge") {
        r = a >= b;
      } else if (name == "gt") {
        r = a > b;
      } else if (name == "le") {
        r = a <= b;
      } else {
        r = a < b;
      }
      push({r ? 1.0 : 0.0, true});
    } else if (name == "true" || name == "false") {
      push({name == "true" ? 1.0 : 0.0, true});
    } else if (name == "not") {
      const Value a = pop();
      if (a.boolean) {
        push({a.number != 0.0 ? 0.0 : 1.0, true});
      } else {
        const auto bits = static_cast<std::uint32_t>(static_cast<std::int64_t>(a.number));
        push({static_cast<double>(static_cast<std::int32_t>(~bits)), false});
      }
    } else if (name == "copy") {
      const std::int64_t n = pop_int();
      if (n < 0 || std::cmp_greater(n, top)) {
        fault("program copies more values than its stack holds");
      }
      const std::size_t start = top - static_cast<std::size_t>(n);
      for (std::size_t i = 0; std::cmp_less(i, n); ++i) {
        push(stack[start + i]);
      }
    } else if (name == "dup") {
      const Value a = pop();
      push(a);
      push(a);
    } else if (name == "exch") {
      const Value b = pop();
      const Value a = pop();
      push(b);
      push(a);
    } else if (name == "index") {
      const std::int64_t n = pop_int();
      if (n < 0 || std::cmp_greater_equal(n, top)) {
        fault("program indexes past its stack");
      }
      push(stack[top - 1 - static_cast<std::size_t>(n)]);
    } else if (name == "pop") {
      (void)pop();
    } else if (name == "roll") {
      const std::int64_t j = pop_int();
      const std::int64_t n = pop_int();
      if (n < 0 || std::cmp_greater(n, top)) {
        fault("program rolls more values than its stack holds");
      }
      if (n > 0) {
        const auto count = static_cast<std::size_t>(n);
        const std::int64_t shift = ((j % n) + n) % n;
        std::rotate(
            stack.begin() + static_cast<std::ptrdiff_t>(top - count),
            stack.begin() + static_cast<std::ptrdiff_t>(top - static_cast<std::size_t>(shift)),
            stack.begin() + static_cast<std::ptrdiff_t>(top));
      }
    }
  }
  // The outputs are the values left on the stack, the last one topmost.
  const std::size_t n = out.size();
  if (top < n) {
    fault("program leaves fewer values than its /Range names");
  }
  for (std::size_t j = 0; j < n; ++j) {
    out[j] = stack[top - n + j].number;
  }
}

}  // namespace pdfkit::images
