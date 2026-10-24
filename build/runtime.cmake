# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# How the compiler runtime is linked. The shared library and the executables
# link libgcc, libstdc++ and winpthread statically, because a P/Invoke caller
# cannot be asked to ship a compiler runtime (CLAUDE.md section 10); the flag
# that achieves it on the pinned toolchain was measured and is recorded in
# build/toolchain.md. ASan's runtime exists only as a DLL under llvm-mingw, so
# the address sanitizer build links the runtime dynamically instead.

function(pdfkit_static_runtime target)
  if(PDFKIT_SANITIZE STREQUAL "address-undefined")
    return()
  endif()
  if(MINGW)
    target_link_options(${target} PRIVATE -static)
  endif()
endfunction()
