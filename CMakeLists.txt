cmake_minimum_required(VERSION 3.20)

project(Tesseract LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# OpenCV configuration
# ============================================================

# Static OpenCV
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_WITH_STATIC_CRT ON CACHE BOOL "" FORCE)

# We don't need Intel IPP for this project.
# Disabling it also avoids the ipphal build issue.
set(WITH_IPP OFF CACHE BOOL "" FORCE)

# Only build the OpenCV modules we actually need
set(BUILD_LIST
    core
    imgproc
    imgcodecs
    highgui
    videoio
    calib3d
    objdetect
    CACHE STRING "" FORCE
)

# Disable unnecessary OpenCV components
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_PERF_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_apps OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_python3 OFF CACHE BOOL "" FORCE)
set(BUILD_JAVA OFF CACHE BOOL "" FORCE)

# Disable generic ASM when using MSVC
if(MSVC)
    set(
        CMAKE_ASM_COMPILER
        NOTFOUND
        CACHE FILEPATH
        "Generic ASM disabled for MSVC"
        FORCE
    )
endif()

# ============================================================
# OpenCV Contrib
# ============================================================

set(
    OPENCV_EXTRA_MODULES_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv_contrib/modules"
    CACHE PATH
    "OpenCV contrib modules"
    FORCE
)

# ============================================================
# Build OpenCV
# ============================================================

add_subdirectory(
    external/opencv
    opencv
)

# ============================================================
# Tesseract
# ============================================================
add_executable(Tesseract
    src/main.cpp
)
add_custom_command(TARGET Tesseract POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:Tesseract>/data/haarcascades"

    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/data/haarcascades"
        "$<TARGET_FILE_DIR:Tesseract>/data/haarcascades"

    COMMENT "Copying OpenCV Haar cascades..."
)
target_include_directories(Tesseract PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"

    "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/include"

    "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/modules/core/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/modules/imgproc/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/modules/imgcodecs/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/modules/highgui/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/modules/videoio/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/modules/calib3d/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/modules/objdetect/include"

    # Generated OpenCV headers
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(Tesseract PRIVATE
    opencv_core
    opencv_imgproc
    opencv_imgcodecs
    opencv_videoio
    opencv_highgui
    opencv_calib3d
    opencv_objdetect
)

# ============================================================
# Link OpenCV
# ============================================================

target_link_libraries(Tesseract
    PRIVATE
    opencv_core
    opencv_imgproc
    opencv_imgcodecs
    opencv_videoio
    opencv_highgui
    opencv_calib3d
    opencv_objdetect
)

# ============================================================
# MSVC Runtime
# ============================================================

if(MSVC)
    set_property(
        TARGET Tesseract
        PROPERTY
        MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()