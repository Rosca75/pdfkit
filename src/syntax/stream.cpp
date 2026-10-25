// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// How a stream names itself in a message: by the indirect object that holds
// it, the identity a caller can find in the file.

#include "syntax/stream.hpp"

#include <string>

namespace pdfkit::syntax {

std::string Stream::describe() const {
  return "object " + std::to_string(owner_.number) + " " + std::to_string(owner_.generation);
}

}  // namespace pdfkit::syntax
