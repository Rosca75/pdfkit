// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The JSON members the open report and inspect share: the encryption the
// document was read through, its repair reasons and its warnings
// (docs/ABI.md, the pdfkit_open and pdfkit_inspect sections). One writer
// keeps the two outputs in the same shape.

#ifndef PDFKIT_API_DOCUMENT_REPORT_HPP
#define PDFKIT_API_DOCUMENT_REPORT_HPP

#include "platform/json_writer.hpp"
#include "syntax/document.hpp"

namespace pdfkit::api {

// Writes the value of "encryption": null, or the handler's description.
void write_encryption(platform::JsonWriter& writer, const syntax::Document& document);

// Writes the value of "repairs": the reasons, in the order they were met.
void write_repairs(platform::JsonWriter& writer, const syntax::Document& document);

// Writes the value of "warnings": a repaired warning carrying the reasons
// when there are any, then each warning with its code, message and page.
void write_warnings(platform::JsonWriter& writer, const syntax::Document& document);

}  // namespace pdfkit::api

#endif  // PDFKIT_API_DOCUMENT_REPORT_HPP
