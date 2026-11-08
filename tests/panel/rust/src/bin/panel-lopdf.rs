// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The lopdf member of the reference panel, a structure reader: loads one PDF
// with lopdf, decrypts it with the empty user password when it is encrypted,
// and writes {"ok": true, "pages": n, "objects": n, "encrypted": bool,
// "decrypted": bool, "mediabox_missing": [page numbers]}, the last naming every
// page whose /MediaBox is reachable neither on the page nor through its
// /Parent chain (an inheritable attribute, ISO 32000-1 section 7.7.3.4). A
// file lopdf cannot load, or a panic, is a refusal, {"ok": false, "error":
// ...}, exit status 2. Source: lopdf 0.47.0's src/reader.rs and
// src/document.rs, read in the crate as published.

use pdfkit_panel as common;

use lopdf::{Document, Object, ObjectId};
use std::panic::{self, AssertUnwindSafe};

/// Whether the page or one of its ancestors holds a /MediaBox; the walk stops
/// after 64 parents, so a /Parent cycle ends.
fn has_media_box(doc: &Document, page: ObjectId) -> bool {
    let mut current = Some(page);
    for _ in 0..64 {
        let Some(id) = current else { return false };
        let Ok(dict) = doc.get_dictionary(id) else { return false };
        if dict.get(b"MediaBox").is_ok() {
            return true;
        }
        current = dict.get(b"Parent").ok().and_then(|parent| match parent {
            Object::Reference(id) => Some(*id),
            _ => None,
        });
    }
    false
}

fn main() {
    let (input, out) = common::arguments();
    panic::set_hook(Box::new(|_| {}));
    let loaded = panic::catch_unwind(|| Document::load(&input));
    let mut doc = match loaded {
        Ok(Ok(doc)) => doc,
        Ok(Err(err)) => common::refuse(&out, &err.to_string()),
        Err(payload) => common::refuse(&out, &common::panic_text(payload.as_ref())),
    };
    let encrypted = doc.is_encrypted() || doc.was_encrypted();
    let mut decrypted = !doc.is_encrypted() && doc.was_encrypted();
    if doc.is_encrypted() {
        decrypted = matches!(
            panic::catch_unwind(AssertUnwindSafe(|| doc.decrypt(""))),
            Ok(Ok(()))
        );
    }
    let summary = panic::catch_unwind(AssertUnwindSafe(|| {
        let pages = doc.get_pages();
        let missing: Vec<String> = pages
            .iter()
            .filter(|(_, id)| !has_media_box(&doc, **id))
            .map(|(number, _)| number.to_string())
            .collect();
        (pages.len(), doc.objects.len(), missing)
    }));
    let (pages, objects, missing) = match summary {
        Ok(summary) => summary,
        Err(payload) => common::refuse(&out, &common::panic_text(payload.as_ref())),
    };
    let body = format!(
        "{{\"ok\": true, \"pages\": {}, \"objects\": {}, \"encrypted\": {}, \"decrypted\": {}, \"mediabox_missing\": [{}]}}",
        pages,
        objects,
        encrypted,
        decrypted,
        missing.join(", ")
    );
    common::finish(&out, &body, true);
}
