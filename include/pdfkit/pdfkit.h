// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The C ABI of pdfkit, the one stable surface of the library: .NET calls it
// through P/Invoke, Python through ctypes and Go through cgo. docs/ABI.md owns
// the JSON shapes, the status codes and the versioning rule; CLAUDE.md
// section 2 owns the catalogue each symbol implements. The header is C99.
//
// The calling rules, in brief: strings are UTF-8 and NUL-terminated; sizes are
// size_t; handles are opaque; every function returning pdfkit_status writes its
// result, or on failure a JSON error object with the fields code, failed,
// expected and fix, into the pdfkit_buffer the caller passes, and the caller
// frees that buffer with pdfkit_buffer_free. A handle is used by one thread at
// a time; distinct handles are independent. The ABI only grows.

#ifndef PDFKIT_PDFKIT_H
#define PDFKIT_PDFKIT_H

#include <stddef.h>
#include <stdint.h>

// PDFKIT_API marks each declaration below. A caller leaves it empty and links
// through the import library; the library's own build names its export header
// in PDFKIT_EXPORT_HEADER, which defines it with the platform's export attribute.
#ifdef PDFKIT_EXPORT_HEADER
#include PDFKIT_EXPORT_HEADER
#endif
#ifndef PDFKIT_API
#define PDFKIT_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* The result of every call. The values never change. */
typedef enum pdfkit_status {
  PDFKIT_OK = 0,
  /* A null handle, a null required argument or malformed JSON input. */
  PDFKIT_E_INVALID_ARGUMENT = 1,
  /* The input document was refused; the error's code names why. */
  PDFKIT_E_REFUSED = 2,
  /* The self-check found a redacted string in the output; nothing is returned. */
  PDFKIT_E_SELF_CHECK = 3,
  /* The catalogue function is not part of this build; the error names it. */
  PDFKIT_E_NOT_IMPLEMENTED = 4,
  /* A defect in pdfkit; the error asks for the input to be reported. */
  PDFKIT_E_INTERNAL = 5
} pdfkit_status;

/*
 * Bytes owned by the library until pdfkit_buffer_free. data is followed by a
 * zero byte not counted in size, so a JSON result reads as a C string.
 * reserved belongs to the library. Pass a zeroed or freed buffer: a call
 * overwrites it without freeing what it held.
 */
typedef struct pdfkit_buffer {
  const uint8_t* data;
  size_t size;
  void* reserved;
} pdfkit_buffer;

/* An open document; created by pdfkit_open, destroyed by pdfkit_close. */
typedef struct pdfkit_document pdfkit_document;

/*
 * An edit plan over an open document; created by pdfkit_edit_new, destroyed by
 * pdfkit_edit_free, always before its document is closed.
 */
typedef struct pdfkit_edit pdfkit_edit;

/* ---- Lifecycle ---------------------------------------------------------- */

/* The ABI version, raised on every addition to this header. */
PDFKIT_API uint32_t pdfkit_abi_version(void);

/*
 * JSON {"version", "abi", "build", "catalogue"}: the library version, the ABI
 * version, the compiler and build type, and per catalogue function whether
 * this build implements it.
 */
PDFKIT_API pdfkit_status pdfkit_version(pdfkit_buffer* out);

/* Frees a buffer a call filled and zeroes it. A zeroed buffer or NULL is a no-op. */
PDFKIT_API void pdfkit_buffer_free(pdfkit_buffer* buffer);

/* Closes a document. NULL is a no-op. Free its edits first. */
PDFKIT_API void pdfkit_close(pdfkit_document* doc);

/* Creates an empty edit plan over doc. */
PDFKIT_API pdfkit_status pdfkit_edit_new(pdfkit_document* doc, pdfkit_edit** out_edit, pdfkit_buffer* out);

/* Frees an edit plan. NULL is a no-op. */
PDFKIT_API void pdfkit_edit_free(pdfkit_edit* edit);

/* ---- Reading ------------------------------------------------------------ */

/*
 * Catalogue 1.1, Open. bytes may be NULL only when size is 0; the library
 * copies what it needs. options_json may be NULL for the defaults. On success
 * *out_doc receives the handle and out the open report.
 */
PDFKIT_API pdfkit_status pdfkit_open(const uint8_t* bytes, size_t size, const char* options_json,
                          pdfkit_document** out_doc, pdfkit_buffer* out);

/* Catalogue 1.2, Inspect. options_json may be NULL. */
PDFKIT_API pdfkit_status pdfkit_inspect(pdfkit_document* doc, const char* options_json, pdfkit_buffer* out);

/* Catalogue 2, Extract text. options_json may be NULL. */
PDFKIT_API pdfkit_status pdfkit_text(pdfkit_document* doc, const char* options_json, pdfkit_buffer* out);

/* Catalogue 3.1, List images. options_json may be NULL. */
PDFKIT_API pdfkit_status pdfkit_images(pdfkit_document* doc, const char* options_json, pdfkit_buffer* out);

/*
 * Catalogue 3.2, Image pixels. identity is an image identity ("img:" and 16
 * hexadecimal digits). On success out holds PNG or JPEG bytes, not JSON.
 */
PDFKIT_API pdfkit_status pdfkit_image_data(pdfkit_document* doc, const char* identity,
                                const char* options_json, pdfkit_buffer* out);

/* Catalogue 4, Extract metadata. options_json may be NULL. */
PDFKIT_API pdfkit_status pdfkit_metadata(pdfkit_document* doc, const char* options_json, pdfkit_buffer* out);

/* Catalogue 8, Strip hidden channels: the report of what is present. */
PDFKIT_API pdfkit_status pdfkit_channels(pdfkit_document* doc, const char* options_json, pdfkit_buffer* out);

/* ---- Editing ------------------------------------------------------------ */

/* Catalogue 5, Redact text. matches_json is required. */
PDFKIT_API pdfkit_status pdfkit_redact_text(pdfkit_edit* edit, const char* matches_json, pdfkit_buffer* out);

/* Catalogue 6, Redact images. treatments_json is required. */
PDFKIT_API pdfkit_status pdfkit_redact_images(pdfkit_edit* edit, const char* treatments_json,
                                   pdfkit_buffer* out);

/* Catalogue 7, Rewrite metadata. fields_json is required. */
PDFKIT_API pdfkit_status pdfkit_set_metadata(pdfkit_edit* edit, const char* fields_json, pdfkit_buffer* out);

/* Catalogue 9, Select pages. pages_json is required. */
PDFKIT_API pdfkit_status pdfkit_select_pages(pdfkit_edit* edit, const char* pages_json, pdfkit_buffer* out);

/*
 * Catalogue 10.1, Write. On success out_pdf holds the PDF bytes and out the
 * write report; on failure out_pdf is left empty and out holds the error.
 * options_json may be NULL.
 */
PDFKIT_API pdfkit_status pdfkit_write(pdfkit_edit* edit, const char* options_json, pdfkit_buffer* out_pdf,
                           pdfkit_buffer* out);

/*
 * Catalogue 10.2, Check: every surviving occurrence of the strings in any PDF
 * bytes. bytes may be NULL only when size is 0. strings_json is required.
 */
PDFKIT_API pdfkit_status pdfkit_check(const uint8_t* bytes, size_t size, const char* strings_json,
                           pdfkit_buffer* out);

#ifdef __cplusplus
}
#endif

#endif /* PDFKIT_PDFKIT_H */
