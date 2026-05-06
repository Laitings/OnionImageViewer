cmake_minimum_required(VERSION 3.21)

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
)

target_link_libraries(OnionImageViewer
    PRIVATE
        Qt6::Widgets
)

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
