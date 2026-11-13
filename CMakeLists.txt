cmake_minimum_required(VERSION 3.21)

# Use vcpkg for image format dependencies when available and no toolchain was chosen: VCPKG_ROOT if set,
# otherwise the copy that ships with Visual Studio (VSINSTALLDIR is set by the developer prompt).
# This must come before project(). A build folder configured without it has to be recreated.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
        set(ONION_VCPKG_ROOT "$ENV{VCPKG_ROOT}")
    elseif(DEFINED ENV{VSINSTALLDIR} AND EXISTS "$ENV{VSINSTALLDIR}/VC/vcpkg/scripts/buildsystems/vcpkg.cmake")
        set(ONION_VCPKG_ROOT "$ENV{VSINSTALLDIR}/VC/vcpkg")
    endif()
    if(ONION_VCPKG_ROOT)
        file(TO_CMAKE_PATH "${ONION_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" ONION_VCPKG_TOOLCHAIN)
        set(CMAKE_TOOLCHAIN_FILE "${ONION_VCPKG_TOOLCHAIN}" CACHE STRING "vcpkg toolchain")
    endif()
endif()

# vcpkg triplet (triplets/x64-windows-onion.cmake): the same /MD runtime as Qt, OpenEXR linked
# statically, and the LGPL libraries libheif, libde265 and LibRaw as DLLs next to the .exe.
# Build folders configured before HEIF support used x64-windows-static-md; they are switched over.
if(CMAKE_HOST_WIN32)
    set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_SOURCE_DIR}/triplets" CACHE STRING "vcpkg overlay triplets")
    if(NOT DEFINED VCPKG_TARGET_TRIPLET OR VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-static-md")
        set(VCPKG_TARGET_TRIPLET "x64-windows-onion" CACHE STRING "vcpkg triplet" FORCE)
    endif()
endif()

project(OnionImageViewer
    VERSION 1.0.3
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_add_executable(OnionImageViewer
    WIN32
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/UpdateChecker.h
    src/UpdateChecker.cpp
    src/FolderTreePanel.h
    src/FolderTreePanel.cpp
    src/ImageViewport.h
    src/ImageViewport.cpp
    src/ImageLoader.h
    src/ImageLoader.cpp
    src/DpxReader.h
    src/DpxReader.cpp
    src/ExrReader.h
    src/ExrReader.cpp
    src/HeifReader.h
    src/HeifReader.cpp
    src/DngReader.h
    src/DngReader.cpp
    src/Exif.h
    src/Exif.cpp
    src/PsdReader.h
    src/PsdReader.cpp
    src/VectorReader.h
    src/VectorReader.cpp
    src/ColorTransform.h
    src/ColorTransform.cpp
    src/Metadata.h
    src/Metadata.cpp
    src/MetadataPanel.h
    src/MetadataPanel.cpp
    src/FrameLoader.h
    src/FrameLoader.cpp
    src/OverlayButton.h
    src/OverlayButton.cpp
    src/PathBar.h
    src/PathBar.cpp
    src/Icons.h
    src/Icons.cpp
    src/FileItemDelegate.h
    src/FileItemDelegate.cpp
    src/FileOps.h
    src/FileOps.cpp
)

target_link_libraries(OnionImageViewer
    PRIVATE
        Qt6::Widgets
        Qt6::Network
)

# App logo: the PNG is built in for the window icon and the About box; the .ico becomes the
# .exe's icon on Windows.
qt_add_resources(OnionImageViewer "app_icons"
    PREFIX "/"
    BASE resources
    FILES
        resources/icons/OnionImageViewer.png
        resources/icons/OnionImageViewer.svg
)
# The version the app shows (About box, the .exe's file properties). The release workflow passes the
# GitHub release tag (-DONION_VERSION=1.2.0); otherwise it is the project VERSION above.
set(ONION_VERSION "" CACHE STRING "Version shown by the app (default: the project VERSION)")
if(ONION_VERSION)
    string(REGEX REPLACE "^[vV]" "" ONION_VERSION_STRING "${ONION_VERSION}")
else()
    set(ONION_VERSION_STRING "${PROJECT_VERSION}")
endif()
# Windows wants four plain numbers: 1.2.0-beta becomes 1,2,0,0.
string(REGEX MATCH "^[0-9]+(\\.[0-9]+)*" ONION_VERSION_NUMBERS "${ONION_VERSION_STRING}")
string(REPLACE "." ";" ONION_VERSION_PARTS "${ONION_VERSION_NUMBERS}")
list(APPEND ONION_VERSION_PARTS 0 0 0 0)
list(SUBLIST ONION_VERSION_PARTS 0 4 ONION_VERSION_PARTS)
list(JOIN ONION_VERSION_PARTS "," ONION_VERSION_COMMAS)
message(STATUS "OnionImageViewer version: ${ONION_VERSION_STRING} (${ONION_VERSION_COMMAS})")
configure_file(src/Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h" @ONLY)
target_include_directories(OnionImageViewer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

if(WIN32)
    target_sources(OnionImageViewer PRIVATE resources/OnionImageViewer.rc)
endif()

# ARRI's LogC3/LogC4 -> Rec.709 LUTs are copied to luts/ next to the .exe, where the ARRI views
# pick them up. They come from resources/luts/arri in the repository; ONION_ARRI_LUT_DIR points at
# another folder of ARRI LUT packages instead:
#   cmake -B build-msvc -DONION_ARRI_LUT_DIR=Q:/Davinci_Shared_LUTs/Arri
set(ONION_ARRI_LUT_DIR "" CACHE PATH "Folder with ARRI LogC3/LogC4 to Rec.709 .cube files to copy next to the .exe (default: resources/luts/arri)")
set(ONION_ARRI_LUT_SOURCE "${ONION_ARRI_LUT_DIR}")
if(NOT ONION_ARRI_LUT_SOURCE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/luts/arri")
    set(ONION_ARRI_LUT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/resources/luts/arri")
endif()
if(ONION_ARRI_LUT_SOURCE)
    file(GLOB_RECURSE ONION_ARRI_LUTS
        "${ONION_ARRI_LUT_SOURCE}/*LogC3-to-Gamma24_Rec709*.cube"
        "${ONION_ARRI_LUT_SOURCE}/*LogC4-to-Gamma24_Rec709*.cube")
    if(ONION_ARRI_LUTS)
        file(COPY ${ONION_ARRI_LUTS} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/luts")
        file(GLOB ONION_ARRI_COPIED "${CMAKE_CURRENT_BINARY_DIR}/luts/*.cube")
        list(LENGTH ONION_ARRI_COPIED ONION_ARRI_LUT_COUNT)
        message(STATUS "ARRI LUTs in luts/: ${ONION_ARRI_LUT_COUNT}")
    else()
        message(WARNING "No ARRI LogC3/LogC4 Rec709 .cube files found in ${ONION_ARRI_LUT_SOURCE}")
    endif()
endif()

# EXR support is optional: without OpenEXR the viewer still builds, just without .exr files.
find_package(OpenEXR CONFIG QUIET)
if(OpenEXR_FOUND)
    message(STATUS "OpenEXR ${OpenEXR_VERSION} found: EXR support enabled")
    target_link_libraries(OnionImageViewer PRIVATE OpenEXR::OpenEXR)
    target_compile_definitions(OnionImageViewer PRIVATE ONION_HAVE_OPENEXR)
else()
    message(STATUS "OpenEXR not found: building without EXR support")
endif()

# HEIF/HEIC support is optional as well: libheif with the libde265 HEVC decoder (both LGPL, so they
# are DLLs on Windows; see triplets/). The DLLs and their licences are copied next to the .exe.
find_package(libheif CONFIG QUIET)
if(libheif_FOUND AND TARGET heif)
    message(STATUS "libheif ${libheif_VERSION} found: HEIF/HEIC support enabled")
    target_link_libraries(OnionImageViewer PRIVATE heif)
    target_compile_definitions(OnionImageViewer PRIVATE ONION_HAVE_LIBHEIF)
else()
    message(STATUS "libheif not found: building without HEIF/HEIC support")
endif()

# LibRaw's reentrant library can decode on multiple viewer and thumbnail workers.
find_package(libraw CONFIG QUIET)
if(TARGET libraw::raw_r)
    message(STATUS "LibRaw found: DNG support enabled")
    target_link_libraries(OnionImageViewer PRIVATE libraw::raw_r)
    target_compile_definitions(OnionImageViewer PRIVATE ONION_HAVE_LIBRAW)
else()
    message(STATUS "LibRaw not found: building without DNG support")
endif()

if(WIN32 AND VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
    set(ONION_VCPKG_PREFIX "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    if(libheif_FOUND AND TARGET heif)
        file(GLOB ONION_HEIF_DLLS "${ONION_VCPKG_PREFIX}/bin/heif.dll" "${ONION_VCPKG_PREFIX}/bin/libde265.dll")
        if(ONION_HEIF_DLLS)
            add_custom_command(TARGET OnionImageViewer POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ONION_HEIF_DLLS} "$<TARGET_FILE_DIR:OnionImageViewer>"
                VERBATIM)
        endif()
    endif()
    if(TARGET libraw::raw_r)
        add_custom_command(TARGET OnionImageViewer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:libraw::raw_r>" "$<TARGET_FILE_DIR:OnionImageViewer>"
            VERBATIM)
    endif()
    foreach(ONION_NOTICE_LIB libheif libde265 libraw jasper lcms libjpeg-turbo zlib)
        if(EXISTS "${ONION_VCPKG_PREFIX}/share/${ONION_NOTICE_LIB}/copyright")
            configure_file("${ONION_VCPKG_PREFIX}/share/${ONION_NOTICE_LIB}/copyright"
                "${CMAKE_CURRENT_BINARY_DIR}/licenses/${ONION_NOTICE_LIB}.txt" COPYONLY)
        endif()
    endforeach()
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/resources/THIRD-PARTY-NOTICES.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/licenses/THIRD-PARTY-NOTICES.txt" COPYONLY)
endif()

# PDF and Illustrator support is optional too: it needs the Qt PDF module (Qt Maintenance Tool:
# Extensions > Qt PDF). EPS files render through Ghostscript at run time when it's installed.
find_package(Qt6 QUIET COMPONENTS Pdf)
if(TARGET Qt6::Pdf)
    message(STATUS "Qt PDF found: PDF/Illustrator support enabled")
    target_link_libraries(OnionImageViewer PRIVATE Qt6::Pdf)
    target_compile_definitions(OnionImageViewer PRIVATE ONION_HAVE_QTPDF)
else()
    message(STATUS "Qt PDF not found: PDF/Illustrator files need Ghostscript")
endif()

target_compile_definitions(OnionImageViewer
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)

if (MSVC)
    target_compile_options(OnionImageViewer PRIVATE /W4 /permissive-)
else()
    target_compile_options(OnionImageViewer PRIVATE -Wall -Wextra -Wpedantic)
endif()
