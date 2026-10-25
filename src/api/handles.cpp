// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Creation of the handles and the null checks that guard every use of one.

#include "api/handles.hpp"

#include "platform/error.hpp"

#include <string>
#include <utility>

namespace pdfkit::api {

std::unique_ptr<pdfkit_document> new_document(std::unique_ptr<syntax::Document> document) {
  auto handle = std::make_unique<pdfkit_document>();
  handle->document = std::move(document);
  return handle;
}

std::unique_ptr<pdfkit_edit> new_edit(pdfkit_document& document) {
  auto edit = std::make_unique<pdfkit_edit>();
  edit->document = &document;
  edit->plan = std::make_unique<redact::EditPlan>(*document.document);
  return edit;
}

pdfkit_document& require_document(pdfkit_document* doc, std::string_view symbol) {
  if (doc == nullptr || doc->document == nullptr) {
    throw platform::Error(platform::ErrorCode::invalid_argument,
                          std::string(symbol) + " was called with a null document handle",
                          "a handle returned by pdfkit_open and not yet closed",
                          "call pdfkit_open first and pass its handle only when it returned "
                          "PDFKIT_OK");
  }
  return *doc;
}

pdfkit_edit& require_edit(pdfkit_edit* edit, std::string_view symbol) {
  if (edit == nullptr) {
    throw platform::Error(platform::ErrorCode::invalid_argument,
                          std::string(symbol) + " was called with a null edit handle",
                          "a handle returned by pdfkit_edit_new and not yet freed",
                          "call pdfkit_edit_new on an open document first and pass its handle "
                          "only when it returned PDFKIT_OK");
  }
  return *edit;
}

}  // namespace pdfkit::api
