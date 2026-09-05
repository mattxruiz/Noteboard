cmake_minimum_required(VERSION 3.16)
project(ProjectBoard VERSION 2.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ProjectBoard WIN32
    src/main.cpp
    src/App.cpp
    src/Storage.cpp
    src/MediaViewer.cpp
)

target_include_directories(ProjectBoard PRIVATE src)

if(MSVC)
    target_compile_options(ProjectBoard PRIVATE /W4 /utf-8)
    target_compile_definitions(ProjectBoard PRIVATE UNICODE _UNICODE NOMINMAX)
else()
    target_compile_options(ProjectBoard PRIVATE -Wall -Wextra)
    target_compile_definitions(ProjectBoard PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_options(ProjectBoard PRIVATE -mwindows)
endif()

target_link_libraries(ProjectBoard PRIVATE
    gdiplus
    comdlg32
    shell32
    ole32
    msimg32
    dwmapi
    mfplat
    mfplay
    mfuuid
)
