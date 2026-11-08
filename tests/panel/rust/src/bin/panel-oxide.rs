// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The pdf_oxide member of the reference panel: reads one PDF with pdf_oxide and
// writes, per page, the text `PdfDocument::extract_text` gives and the visible
// box `PdfDocument::get_page_visible_box` gives (the crop box reduced to the
// media box), as {"ok": true, "pages": [{"text": ..., "box": [x0, y0, x1, y1]}]}.
// A page the crate cannot read, or that panics, keeps its place with a null
// text and its error, so the other pages still vote; a document it cannot open
// is a refusal, {"ok": false, "error": ...}, with exit status 2.
// Source: pdf_oxide 0.3.78's src/document.rs, read in the crate as published.

use pdfkit_panel as common;

use pdf_oxide::PdfDocument;
use std::panic::{self, AssertUnwindSafe};

fn main() {
    let (input, out) = common::arguments();
    panic::set_hook(Box::new(|_| {}));
    let opened = panic::catch_unwind(|| PdfDocument::open(&input));
    let doc = match opened {
        Ok(Ok(doc)) => doc,
        Ok(Err(err)) => common::refuse(&out, &err.to_string()),
        Err(payload) => common::refuse(&out, &common::panic_text(payload.as_ref())),
    };
    let count = match panic::catch_unwind(AssertUnwindSafe(|| doc.page_count())) {
        Ok(Ok(count)) => count,
        Ok(Err(err)) => common::refuse(&out, &err.to_string()),
        Err(payload) => common::refuse(&out, &common::panic_text(payload.as_ref())),
    };
    let mut pages = Vec::with_capacity(count);
    for index in 0..count {
        let text = panic::catch_unwind(AssertUnwindSafe(|| doc.extract_text(index)));
        let bounds = panic::catch_unwind(AssertUnwindSafe(|| doc.get_page_visible_box(index)));
        let mut page = String::from("{");
        match text {
            Ok(Ok(text)) => page.push_str(&format!("\"text\": {}", common::json_string(&text))),
            Ok(Err(err)) => page.push_str(&format!(
                "\"text\": null, \"error\": {}",
                common::json_string(&err.to_string())
            )),
            Err(payload) => page.push_str(&format!(
                "\"text\": null, \"error\": {}",
                common::json_string(&common::panic_text(payload.as_ref()))
            )),
        }
        match bounds {
            Ok(Ok((x0, y0, x1, y1))) => page.push_str(&format!(
                ", \"box\": [{}, {}, {}, {}]",
                common::json_number(x0),
                common::json_number(y0),
                common::json_number(x1),
                common::json_number(y1)
            )),
            _ => page.push_str(", \"box\": null"),
        }
        page.push('}');
        pages.push(page);
    }
    let body = format!("{{\"ok\": true, \"pages\": [{}]}}", pages.join(", "));
    common::finish(&out, &body, true);
}
