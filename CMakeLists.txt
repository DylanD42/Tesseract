
cmake_minimum_required(VERSION 3.20)

project(Tesseract LANGUAGES C CXX)

# ============================================================
# Compiler settings
# ============================================================

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Use static MSVC runtime (/MT or /MTd)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

# ============================================================
# Build options
# ============================================================

option(BUILD_HEADTRACKING "Build head tracking application" ON)
option(BUILD_RAYLIB_SCENE "Build raylib application" ON)
option(BUILD_PARALLAX "Build integrated parallax application" ON)

# ============================================================
# Output directories
# ============================================================

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/bin"
)

# ============================================================
# raylib
# ============================================================

if(BUILD_RAYLIB_SCENE OR BUILD_PARALLAX)

    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

    add_subdirectory(
        external/raylib
        raylib_build
    )

endif()

# ============================================================
# OpenCV and dlib
# ============================================================

if(BUILD_HEADTRACKING OR BUILD_PARALLAX)

    include(FetchContent)

    # --------------------------------------------------------
    # dlib configuration
    # --------------------------------------------------------

    set(DLIB_NO_GUI_SUPPORT ON CACHE BOOL "" FORCE)
    set(DLIB_USE_CUDA OFF CACHE BOOL "" FORCE)
    set(DLIB_JPEG_SUPPORT OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        dlib
        GIT_REPOSITORY https://github.com/davisking/dlib.git
        GIT_TAG v19.24.6
        GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(dlib)

    # --------------------------------------------------------
    # OpenCV configuration
    # --------------------------------------------------------

    set(BUILD_WITH_STATIC_CRT ON CACHE BOOL "" FORCE)
    set(WITH_IPP OFF CACHE BOOL "" FORCE)

    set(BUILD_LIST
        "core,imgproc,imgcodecs,highgui,videoio,calib3d,objdetect,dnn"
        CACHE STRING "" FORCE
    )

    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_PERF_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_opencv_apps OFF CACHE BOOL "" FORCE)
    set(BUILD_opencv_python3 OFF CACHE BOOL "" FORCE)
    set(BUILD_JAVA OFF CACHE BOOL "" FORCE)

    if(MSVC)
        set(
            CMAKE_ASM_COMPILER
            NOTFOUND
            CACHE FILEPATH "Generic ASM disabled"
            FORCE
        )
    endif()

    # --------------------------------------------------------
    # OpenCV Contrib
    # --------------------------------------------------------

    set(
        OPENCV_EXTRA_MODULES_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv_contrib/modules"
        CACHE PATH "OpenCV Contrib modules" FORCE
    )

    # --------------------------------------------------------
    # Build OpenCV
    # --------------------------------------------------------

    add_subdirectory(
        external/opencv
        opencv_build
    )

    # --------------------------------------------------------
    # Shared OpenCV and dlib configuration
    # --------------------------------------------------------

    # Reuse these settings for both applications.

    function(configure_headtracking_libraries target_name)

        target_include_directories(${target_name} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"

            "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/include"

            "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/modules/core/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/modules/imgproc/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/modules/imgcodecs/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/modules/highgui/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/modules/videoio/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/modules/calib3d/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/modules/objdetect/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/modules/dnn/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/modules/features2d/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/modules/flann/include"

            # Generated OpenCV headers
            "${CMAKE_CURRENT_BINARY_DIR}/opencv_build"
            "${CMAKE_CURRENT_BINARY_DIR}"
        )

        target_link_libraries(${target_name} PRIVATE
            dlib::dlib

            opencv_core
            opencv_imgproc
            opencv_imgcodecs
            opencv_videoio
            opencv_highgui
            opencv_calib3d
            opencv_objdetect
            opencv_dnn
        )

        # Copy facial landmark models
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/custom")

            add_custom_command(
                TARGET ${target_name} POST_BUILD

                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_CURRENT_SOURCE_DIR}/custom"
                    "$<TARGET_FILE_DIR:${target_name}>/custom"

                COMMENT "Copying models for ${target_name}..."
            )

        endif()

        # Copy Haar cascades
        if(EXISTS
           "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/data/haarcascades")

            add_custom_command(
                TARGET ${target_name} POST_BUILD

                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv/data/haarcascades"
                    "$<TARGET_FILE_DIR:${target_name}>/data/haarcascades"

                COMMENT "Copying Haar cascades for ${target_name}..."
            )

        endif()

    endfunction()

endif()

# ============================================================
# Original raylib scene
# ============================================================

if(BUILD_RAYLIB_SCENE)

    add_executable(RaylibScene
        src/raylib/newplace.cpp
    )

    target_link_libraries(RaylibScene PRIVATE
        raylib
    )

endif()

# ============================================================
# Original head tracking application
# ============================================================

if(BUILD_HEADTRACKING)

    add_executable(HeadTracking
        src/headtracking/main.cpp
    )

    configure_headtracking_libraries(HeadTracking)

endif()

# ============================================================
# NEW: Integrated head tracking + raylib parallax
# ============================================================

if(BUILD_PARALLAX)

    add_executable(TesseractParallax
        src/both/Tesseract_Integrated_Parallax.cpp
    )

    # Link OpenCV and dlib
    configure_headtracking_libraries(TesseractParallax)

    # Link raylib
    target_link_libraries(TesseractParallax PRIVATE
        raylib
    )

endif()
