
cmake_minimum_required(VERSION 3.20)

project(Tesseract LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use the static MSVC runtime
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

# raylib options
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Build raylib from source
add_subdirectory(
    "${CMAKE_SOURCE_DIR}/external/raylib"
    "${CMAKE_BINARY_DIR}/raylib_build"
)

# Build only the raylib program
add_executable(program
    src/newplace.cpp
)

target_link_libraries(program PRIVATE
    raylib
)

# Set executable output directory
set_target_properties(program PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(MSVC)
    target_compile_options(program PRIVATE /W3)
endif()
