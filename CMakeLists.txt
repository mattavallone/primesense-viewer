cmake_minimum_required(VERSION 3.18)
project(PrimeSenseViewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OPENNI2_ROOT "C:/Program Files/OpenNI2" CACHE PATH
    "OpenNI2 installation directory")
find_path(OPENNI2_INCLUDE_DIR OpenNI.h
    HINTS "${OPENNI2_ROOT}/Include"
)
if(NOT OPENNI2_INCLUDE_DIR)
    message(FATAL_ERROR "OpenNI.h not found. Set OPENNI2_ROOT to the OpenNI2 installation.")
endif()
find_library(OPENNI2_LIBRARY NAMES OpenNI2
    HINTS "${OPENNI2_ROOT}/Lib"
)
if(NOT OPENNI2_LIBRARY)
    message(FATAL_ERROR "OpenNI2 library not found. Set OPENNI2_ROOT to the OpenNI2 installation.")
endif()
find_package(OpenCV REQUIRED)

add_executable(primesense_viewer primesense_viewer.cpp)
target_include_directories(primesense_viewer PRIVATE
    "${OPENNI2_INCLUDE_DIR}"
    ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(primesense_viewer PRIVATE
    "${OPENNI2_LIBRARY}"
    ${OpenCV_LIBS}
)

if(WIN32)
    set(PRIMESENSE_RUNTIME_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/../primesense-openni2-winusb"
        CACHE PATH "Path to the primesense-openni2-winusb repository")
    find_program(POWERSHELL_EXECUTABLE NAMES powershell.exe powershell)
    if(NOT POWERSHELL_EXECUTABLE)
        message(FATAL_ERROR "PowerShell is required to stage the OpenNI2 runtime.")
    endif()
    add_custom_command(TARGET primesense_viewer POST_BUILD
        COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
            -File "${PRIMESENSE_RUNTIME_DIR}/Prepare-OpenNI2-Runtime.ps1"
            -RedistDir "${OPENNI2_ROOT}/Redist"
            -OutputDir "$<TARGET_FILE_DIR:primesense_viewer>"
        VERBATIM
    )
endif()
