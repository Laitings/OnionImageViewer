# vcpkg triplet for OnionImageViewer: 64-bit Windows, the dynamic MSVC runtime (/MD) like Qt.
#
# Libraries are linked statically (OpenEXR and its dependencies), except the LGPL ones: libheif,
# its HEVC decoder libde265 and LibRaw are built as DLLs and shipped next to the .exe, so they can be
# swapped for other builds as the LGPL requires.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

if(PORT MATCHES "^(libheif|libde265|libraw)$")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
