# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# How our binaries are linked. The shared library and the executables link
# libgcc, libstdc++ and winpthread statically, because a P/Invoke caller
# cannot be asked to ship a compiler runtime (CLAUDE.md section 10); the flag
# that achieves it on the pinned toolchain was measured and is recorded in
# build/toolchain.md. ASan's runtime exists only as a DLL under llvm-mingw, so
# the address sanitizer build links the runtime dynamically instead. A release
# build drops unreferenced sections and strips symbols, because the statically
# linked runtime otherwise triples the size of each artefact (build/budgets.tsv).

function(pdfkit_static_runtime target)
  if(MINGW AND NOT PDFKIT_SANITIZE STREQUAL "address-undefined")
    target_link_options(${target} PRIVATE -static)
  endif()
  target_compile_options(${target} PRIVATE
    "$<$<CONFIG:Release>:-ffunction-sections;-fdata-sections>")
  target_link_options(${target} PRIVATE "$<$<CONFIG:Release>:-Wl,--gc-sections;-s>")
  # No link time in the PE header, so one tree links to one set of bytes.
  if(MINGW)
    target_link_options(${target} PRIVATE "$<$<CONFIG:Release>:-Wl,--no-insert-timestamp>")
  endif()
endfunction()
