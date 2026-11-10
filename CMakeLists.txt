cmake_minimum_required(VERSION 3.20)

project(Tesseract LANGUAGES C CXX)

# ============================================================
# Compiler settings
# ============================================================

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Static MSVC runtime (/MT or /MTd) so the exe has no DLL dependencies.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ============================================================
# raylib (git submodule)
# ============================================================

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

add_subdirectory(external/raylib raylib_build)

# ============================================================
# dlib (fetched on first configure; needs network once)
# ============================================================

include(FetchContent)

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

# ============================================================
# OpenCV (git submodule, built from source with only the modules we use)
# ============================================================

set(BUILD_WITH_STATIC_CRT ON CACHE BOOL "" FORCE)
set(WITH_IPP OFF CACHE BOOL "" FORCE)

set(BUILD_LIST "core,imgproc,imgcodecs,highgui,videoio,calib3d,objdetect,dnn" CACHE STRING "" FORCE)

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_PERF_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_apps OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_python3 OFF CACHE BOOL "" FORCE)
set(BUILD_JAVA OFF CACHE BOOL "" FORCE)

if(MSVC)
    set(CMAKE_ASM_COMPILER NOTFOUND CACHE FILEPATH "Generic ASM disabled" FORCE)
endif()

set(OPENCV_EXTRA_MODULES_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv_contrib/modules"
    CACHE PATH "OpenCV Contrib modules" FORCE)

add_subdirectory(external/opencv opencv_build)

set(TESSERACT_OPENCV_INCLUDES
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
    # Generated OpenCV headers (cvconfig.h, opencv_modules.hpp)
    "${CMAKE_CURRENT_BINARY_DIR}/opencv_build"
    "${CMAKE_CURRENT_BINARY_DIR}"
)

# ============================================================
# Tesseract
# ============================================================
#
#   src/main.cpp           wiring: load models, start tracker, run the frame loop
#   src/core/              config (saved to tesseract_calibration.yml) + app state shared with the UI
#   src/tracking/          webcam -> YuNet -> dlib -> solvePnP -> filtered eye position (own threads)
#   src/render/            off-axis projection, scene shader, procedural textures, the scenes
#   src/ui/                start menu, scene picker, settings panel, calibration pages, HUD. New UI goes here.

add_executable(Tesseract
    src/main.cpp
    src/core/config.cpp
    src/core/text.cpp
    src/core/app_state.cpp
    src/core/assets.cpp
    src/tracking/head_tracker.cpp
    src/tracking/camera_list.cpp
    src/render/viewer.cpp
    src/render/scene_shader.cpp
    src/render/scene_textures.cpp
    src/render/scene_helpers.cpp
    src/render/scenes.cpp
    src/ui/ui_widgets.cpp
    src/ui/hotkeys.cpp
    src/ui/hud.cpp
    src/ui/menu_ui.cpp
    src/ui/settings_ui.cpp
    src/ui/calibration_ui.cpp
    src/ui/webcam_pip.cpp
    src/ui/file_dialog.cpp
)

target_include_directories(Tesseract PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    ${TESSERACT_OPENCV_INCLUDES}
)

target_link_libraries(Tesseract PRIVATE
    raylib
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

if(WIN32)
    # DirectShow camera enumeration and the native file dialog.
    target_link_libraries(Tesseract PRIVATE strmiids ole32 oleaut32 comdlg32)
endif()

# Last-resort location of the model files when running from the IDE or a build tree
# without the copied custom/ folder (see src/core/assets.cpp).
target_compile_definitions(Tesseract PRIVATE
    TESSERACT_SOURCE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/custom"
)

# Put the models next to the executable so it runs from any working directory.
add_custom_command(
    TARGET Tesseract POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/custom"
        "$<TARGET_FILE_DIR:Tesseract>/custom"
    COMMENT "Copying face models next to Tesseract..."
)
