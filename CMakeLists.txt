cmake_minimum_required(VERSION 3.21)

# Use vcpkg (it ships with Visual Studio) for OpenEXR when it's available and no toolchain was
# chosen. Must come before project(). A build folder configured without it has to be recreated.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT}
        AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "vcpkg toolchain")
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
)

target_link_libraries(OnionImageViewer
    PRIVATE
        Qt6::Widgets
)

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
