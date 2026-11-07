// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The pdf-extract member of the reference panel: reads one PDF with
// pdf-extract's `extract_text_from_mem_by_pages` (which decrypts a file whose
// user password is empty) and writes {"ok": true, "pages": [{"text": ...}]}.
// That call walks the pages from the first and stops at the first page it
// cannot read, so its page list can be shorter than the document: the adapter
// reports what the crate returns and does not repair it. A document it cannot
// open, or a panic, is a refusal, {"ok": false, "error": ...}, exit status 2.
// Source: pdf-extract 0.12.1's src/lib.rs, read in the crate as published.

use pdfkit_panel as common;

use std::panic::{self, AssertUnwindSafe};

fn main() {
    let (input, out) = common::arguments();
    panic::set_hook(Box::new(|_| {}));
    let bytes = match std::fs::read(&input) {
        Ok(bytes) => bytes,
        Err(err) => common::refuse(&out, &format!("cannot read the input: {}", err)),
    };
    let pages = match panic::catch_unwind(AssertUnwindSafe(|| {
        pdf_extract::extract_text_from_mem_by_pages(&bytes)
    })) {
        Ok(Ok(pages)) => pages,
        Ok(Err(err)) => common::refuse(&out, &err.to_string()),
        Err(payload) => common::refuse(&out, &common::panic_text(payload.as_ref())),
    };
    let listed: Vec<String> = pages
        .iter()
        .map(|text| format!("{{\"text\": {}}}", common::json_string(text)))
        .collect();
    let body = format!("{{\"ok\": true, \"pages\": [{}]}}", listed.join(", "));
    common::finish(&out, &body, true);
}
