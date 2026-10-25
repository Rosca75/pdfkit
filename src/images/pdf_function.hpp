// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// PdfFunction is a PDF function of ISO 32000-1 section 7.10 (recalled; pdf.js
// src/core/function.js, Apache-2.0, as the reader beside it, recalled), the
// tint transform of a Separation or DeviceN colour space: type 0 (sampled,
// 1 to 32 bits per sample, /Encode, /Decode, /Size, multilinear
// interpolation), type 2 (exponential), type 3 (stitching) and type 4 (the
// PostScript calculator). Every input is clipped to /Domain and every output
// to /Range where one is given. A hostile function cannot run away: stitching
// nests at most 8 deep, a sampled table is checked against the decoding budget
// before it is read, a calculator program holds at most 100,000 operators, its
// operand stack at most 100 values, and one evaluation runs at most 100,000
// operators. A function the file states wrongly throws DecodeFault.

#ifndef PDFKIT_IMAGES_PDF_FUNCTION_HPP
#define PDFKIT_IMAGES_PDF_FUNCTION_HPP

#include "syntax/document.hpp"
#include "syntax/object.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace pdfkit::images {

class PdfFunction {
 public:
  static constexpr int max_depth = 8;
  static constexpr std::size_t max_stack = 100;
  static constexpr std::size_t max_operators = 100000;

  // The function the object (a dictionary or a stream, or a reference to
  // one) describes.
  [[nodiscard]] static std::shared_ptr<const PdfFunction> parse(syntax::Document& document,
                                                                const syntax::Object& object,
                                                                int depth = 0);

  // A type 2 function, for tests and for building tint transforms in code.
  [[nodiscard]] static std::shared_ptr<const PdfFunction> exponential(std::vector<double> c0,
                                                                      std::vector<double> c1,
                                                                      double exponent);

  [[nodiscard]] std::size_t inputs() const noexcept { return domain_.size() / 2; }
  // The output count: /Range's, or what the type implies when it has none.
  [[nodiscard]] std::size_t outputs() const noexcept { return outputs_; }

  // Evaluates: in holds inputs() values; out receives outputs() values.
  // Throws DecodeFault when a calculator program fails (a stack under- or
  // overflow, a type error, the operator budget).
  void evaluate(std::span<const double> in, std::span<double> out) const;

  // A calculator instruction: a number to push, an operator, or a jump of an
  // if or ifelse block. Public so the parser in the implementation file can
  // build the list.
  struct Instruction {
    enum class Kind : std::uint8_t { number, op, jump_if_false, jump };
    Kind kind = Kind::number;
    double value = 0.0;
    std::uint16_t op = 0;
    std::size_t target = 0;
  };

 private:
  enum class Type : std::uint8_t { sampled, exponential, stitching, calculator };

  PdfFunction() = default;

  void evaluate_sampled(std::span<const double> in, std::span<double> out) const;
  void evaluate_stitching(std::span<const double> in, std::span<double> out) const;
  void evaluate_calculator(std::span<const double> in, std::span<double> out) const;

  Type type_ = Type::exponential;
  std::vector<double> domain_;
  std::vector<double> range_;
  std::size_t outputs_ = 0;
  // Type 0.
  std::vector<std::uint32_t> size_;
  std::vector<double> encode_;
  std::vector<double> decode_;
  std::vector<double> samples_;
  // Type 2.
  std::vector<double> c0_;
  std::vector<double> c1_;
  double exponent_ = 1.0;
  // Type 3.
  std::vector<std::shared_ptr<const PdfFunction>> functions_;
  std::vector<double> bounds_;
  // Type 4.
  std::vector<Instruction> program_;
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_PDF_FUNCTION_HPP
