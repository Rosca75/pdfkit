// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The library the three adapters share: the JSON string escape and the writing
// of the answer into the file the caller names. The answer goes to a file
// rather than to standard output because the crates print diagnostics of their
// own there.

use std::fs;

/// The JSON form of a string: quoted, with the control characters, the quote
/// and the backslash escaped (RFC 8259 section 7).
pub fn json_string(text: &str) -> String {
    let mut out = String::with_capacity(text.len() + 2);
    out.push('"');
    for c in text.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out.push('"');
    out
}

/// A number as JSON: finite values as written by Rust, anything else as null.
pub fn json_number(value: f32) -> String {
    if value.is_finite() {
        format!("{}", value)
    } else {
        "null".to_string()
    }
}

/// The arguments every adapter takes: the PDF to read and the file the answer goes to.
pub fn arguments() -> (String, String) {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 3 {
        eprintln!("usage: {} <input.pdf> <answer.json>", args.first().map(String::as_str).unwrap_or("panel"));
        std::process::exit(1);
    }
    (args[1].clone(), args[2].clone())
}

/// Writes the answer and exits: 0 for an answer, 2 for the reader's refusal of the file.
pub fn finish(out: &str, body: &str, ok: bool) -> ! {
    if let Err(err) = fs::write(out, body) {
        eprintln!("cannot write {}: {}", out, err);
        std::process::exit(1);
    }
    std::process::exit(if ok { 0 } else { 2 });
}

/// A refusal: the reader could not read the file at all.
pub fn refuse(out: &str, error: &str) -> ! {
    finish(out, &format!("{{\"ok\": false, \"error\": {}}}", json_string(error)), false)
}

/// The text of a caught panic's payload.
pub fn panic_text(payload: &(dyn std::any::Any + Send)) -> String {
    if let Some(text) = payload.downcast_ref::<&str>() {
        format!("panic: {}", text)
    } else if let Some(text) = payload.downcast_ref::<String>() {
        format!("panic: {}", text)
    } else {
        "panic".to_string()
    }
}
