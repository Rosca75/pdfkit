// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of images::PdfFunction over known values: type 0 (sampled, with
// interpolation between samples), type 2, type 3 stitching two functions,
// type 4 programs with arithmetic, comparisons, stack operators and nested
// if and ifelse blocks; and the bounds: a calculator program past the
// operator budget, a stack overflow and a stitching chain past the depth
// bound are refused with a DecodeFault.

#include "images/pdf_function.hpp"

#include "images/decode_fault.hpp"
#include "images/image_fixture.hpp"

#include <doctest.h>

#include <array>
#include <string>
#include <vector>

namespace {

using pdfkit::images::DecodeFault;
using pdfkit::images::PdfFunction;
using pdfkit::test::bytes_of;
using pdfkit::test::ImageFixture;

std::vector<double> run(const PdfFunction& function, std::vector<double> in) {
  std::vector<double> out(function.outputs());
  function.evaluate(in, out);
  return out;
}

std::shared_ptr<const PdfFunction> calculator(const std::string& program, int outputs = 1,
                                              int inputs = 1) {
  ImageFixture fixture;
  std::string domain;
  for (int i = 0; i < inputs; ++i) {
    domain += "0 1 ";
  }
  std::string range;
  for (int i = 0; i < outputs; ++i) {
    range += "-1000 1000 ";
  }
  const std::uint32_t number =
      fixture.stream("/FunctionType 4 /Domain [" + domain + "] /Range [" + range + "]", program);
  auto& document = fixture.open();
  return PdfFunction::parse(document, fixture.get(number));
}

TEST_CASE("a type 2 function interpolates by its exponent") {
  ImageFixture fixture;
  const std::uint32_t number =
      fixture.object("<< /FunctionType 2 /Domain [0 1] /C0 [0 1] /C1 [1 0] /N 2 >>");
  auto& document = fixture.open();
  const auto function = PdfFunction::parse(document, fixture.get(number));
  CHECK(function->inputs() == 1U);
  CHECK(function->outputs() == 2U);
  const auto out = run(*function, {0.5});
  CHECK(out[0] == doctest::Approx(0.25));
  CHECK(out[1] == doctest::Approx(0.75));
  // An input outside /Domain is clipped to it.
  CHECK(run(*function, {7.0})[0] == doctest::Approx(1.0));
}

TEST_CASE("a type 0 function interpolates its samples") {
  ImageFixture fixture;
  // Three samples of one output over [0 1]: 0, 255, 0 at 8 bits.
  const std::uint32_t number =
      fixture.stream("/FunctionType 0 /Domain [0 1] /Range [0 1] /Size [3] /BitsPerSample 8",
                     bytes_of({0, 255, 0}));
  auto& document = fixture.open();
  const auto function = PdfFunction::parse(document, fixture.get(number));
  CHECK(run(*function, {0.0})[0] == doctest::Approx(0.0));
  CHECK(run(*function, {0.25})[0] == doctest::Approx(0.5));
  CHECK(run(*function, {0.5})[0] == doctest::Approx(1.0));
  CHECK(run(*function, {1.0})[0] == doctest::Approx(0.0));
}

TEST_CASE("a two-input type 0 function interpolates bilinearly") {
  ImageFixture fixture;
  // A 2 by 2 table: (0,0) 0, (1,0) 1, (0,1) 1, (1,1) 0 at 8 bits.
  const std::uint32_t number =
      fixture.stream("/FunctionType 0 /Domain [0 1 0 1] /Range [0 1] /Size [2 2] /BitsPerSample 8",
                     bytes_of({0, 255, 255, 0}));
  auto& document = fixture.open();
  const auto function = PdfFunction::parse(document, fixture.get(number));
  CHECK(run(*function, {0.5, 0.5})[0] == doctest::Approx(0.5));
  CHECK(run(*function, {1.0, 0.0})[0] == doctest::Approx(1.0));
  CHECK(run(*function, {0.0, 1.0})[0] == doctest::Approx(1.0));
}

TEST_CASE("a type 3 function stitches its parts over their bounds") {
  ImageFixture fixture;
  const std::uint32_t low =
      fixture.object("<< /FunctionType 2 /Domain [0 1] /C0 [0] /C1 [1] /N 1 >>");
  const std::uint32_t high =
      fixture.object("<< /FunctionType 2 /Domain [0 1] /C0 [1] /C1 [0] /N 1 >>");
  const std::uint32_t number =
      fixture.object("<< /FunctionType 3 /Domain [0 1] /Functions [" + std::to_string(low) +
                     " 0 R " + std::to_string(high) + " 0 R] /Bounds [0.5] /Encode [0 1 0 1] >>");
  auto& document = fixture.open();
  const auto function = PdfFunction::parse(document, fixture.get(number));
  CHECK(run(*function, {0.25})[0] == doctest::Approx(0.5));
  CHECK(run(*function, {0.5})[0] == doctest::Approx(1.0));
  CHECK(run(*function, {0.75})[0] == doctest::Approx(0.5));
}

TEST_CASE("a type 4 program computes with the calculator's operators") {
  CHECK(run(*calculator("{ 2 mul 1 add }"), {0.5})[0] == doctest::Approx(2.0));
  CHECK(run(*calculator("{ dup mul neg abs sqrt }"), {0.5})[0] == doctest::Approx(0.5));
  CHECK(run(*calculator("{ 0.5 gt { 10 } { 20 } ifelse }"), {0.75})[0] == doctest::Approx(10.0));
  CHECK(run(*calculator("{ 0.5 gt { 10 } { 20 } ifelse }"), {0.25})[0] == doctest::Approx(20.0));
  CHECK(run(*calculator("{ pop 3 4 exch sub }"), {0.0})[0] == doctest::Approx(1.0));
  CHECK(run(*calculator("{ pop 1 2 3 3 1 roll pop pop }"), {0.0})[0] == doctest::Approx(3.0));
  CHECK(run(*calculator("{ pop 5 7 1 index add exch pop }"), {0.0})[0] == doctest::Approx(12.0));
  CHECK(run(*calculator("{ pop 7 2 idiv 7 2 mod add }"), {0.0})[0] == doctest::Approx(4.0));
  CHECK(run(*calculator("{ pop 1 3 bitshift 6 xor }"), {0.0})[0] == doctest::Approx(14.0));
  CHECK(run(*calculator("{ pop true false or { 1 { 2 } if } if }"), {0.0})[0] ==
        doctest::Approx(2.0));
  const auto two = calculator("{ 1 exch sub dup }", 2);
  const auto out = run(*two, {0.25});
  CHECK(out[0] == doctest::Approx(0.75));
  CHECK(out[1] == doctest::Approx(0.75));
}

TEST_CASE("a type 4 program past its bounds is refused") {
  // Each evaluation of this program pushes far past the stack's 100 values.
  std::string push;
  for (int i = 0; i < 120; ++i) {
    push += "1 ";
  }
  CHECK_THROWS_AS(run(*calculator("{ " + push + "}"), {0.0}), DecodeFault);
  // More operators than one program may hold.
  std::string many = "{ ";
  for (std::size_t i = 0; i < PdfFunction::max_operators + 1; ++i) {
    many += "dup pop ";
  }
  CHECK_THROWS_AS((void)calculator(many + "}"), DecodeFault);
  CHECK_THROWS_AS((void)calculator("{ 1 add"), DecodeFault);
  CHECK_THROWS_AS((void)calculator("{ 1 frobnicate }"), DecodeFault);
  CHECK_THROWS_AS(run(*calculator("{ pop pop }"), {0.0}), DecodeFault);
}

TEST_CASE("a stitching chain past the depth bound is refused") {
  ImageFixture fixture;
  std::uint32_t inner = fixture.object("<< /FunctionType 2 /Domain [0 1] /C0 [0] /C1 [1] /N 1 >>");
  for (int i = 0; i < PdfFunction::max_depth + 2; ++i) {
    inner = fixture.object("<< /FunctionType 3 /Domain [0 1] /Functions [" + std::to_string(inner) +
                           " 0 R] /Bounds [] /Encode [0 1] >>");
  }
  auto& document = fixture.open();
  CHECK_THROWS_AS((void)PdfFunction::parse(document, fixture.get(inner)), DecodeFault);
}

}  // namespace
