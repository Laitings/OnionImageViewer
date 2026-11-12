cmake_minimum_required(VERSION 3.21)

# Use vcpkg for OpenEXR when it's available and no toolchain was chosen: VCPKG_ROOT if set,
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

# Link vcpkg libraries statically (with the same /MD runtime as Qt), so no extra DLLs have to be
# shipped next to the .exe.
if(CMAKE_HOST_WIN32 AND NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "vcpkg triplet")
endif()

project(OnionImageViewer
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_add_executable(OnionImageViewer
    WIN32
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
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
    src/ColorTransform.h
    src/ColorTransform.cpp
)

target_link_libraries(OnionImageViewer
    PRIVATE
        Qt6::Widgets
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
if(WIN32)
    target_sources(OnionImageViewer PRIVATE resources/OnionImageViewer.rc)
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
