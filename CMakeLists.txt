cmake_minimum_required(VERSION 3.20)

project(Tesseract LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_WITH_STATIC_CRT ON CACHE BOOL "" FORCE)

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

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_PERF_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_apps OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_python3 OFF CACHE BOOL "" FORCE)
set(BUILD_JAVA OFF CACHE BOOL "" FORCE)


if(MSVC)
    set(CMAKE_ASM_COMPILER NOTFOUND CACHE FILEPATH "Generic ASM disabled for MSVC" FORCE)
endif()

set(OPENCV_EXTRA_MODULES_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv_contrib/modules" CACHE PATH "OpenCV contrib modules" FORCE)

add_subdirectory(
    external/opencv
    opencv
)



add_executable(Tesseract
    src/main.cpp
)


target_include_directories(Tesseract
    PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
)


target_include_directories(Tesseract
    SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/external/opencv/include"
    "${OPENCV_CONFIG_FILE_INCLUDE_DIR}"
)


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

foreach(opencv_module IN LISTS OPENCV_MODULES_BUILD)
    target_include_directories(Tesseract SYSTEM PRIVATE
        "${OPENCV_MODULE_${opencv_module}_LOCATION}/include"
    )
endforeach()


if(MSVC)
    set_property(TARGET Tesseract PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()


