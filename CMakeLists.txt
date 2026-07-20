cmake_minimum_required(VERSION 3.20)
project(CODMSpawnSnack CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Prefer vcpkg manifest mode if VCPKG_ROOT is set.
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenCV CONFIG REQUIRED)

# C++/WinRT projection headers (required for WGC and Windows.Media.Ocr).
# The Windows SDK ships these under Include/<ver>/cppwinrt; we point at them
# directly so no extra cppwinrt.exe codegen step is needed.
# Locate the SDK include root without depending on vcvars env vars.
if(MSVC)
    set(_winsdk_root "C:/Program Files (x86)/Windows Kits/10")
    if(DEFINED ENV{WindowsSdkDir} AND DEFINED ENV{WindowsSDKVersion})
        set(_winsdk_include "$ENV{WindowsSdkDir}Include/$ENV{WindowsSDKVersion}")
    else()
        # Pick the highest available version directory under the SDK Include root.
        file(GLOB _winsdk_versions LIST_DIRECTORIES true
             "${_winsdk_root}/Include/*")
        set(_winsdk_include "")
        foreach(_d ${_winsdk_versions})
            if(IS_DIRECTORY "${_d}/cppwinrt")
                set(_winsdk_include "${_d}")
            endif()
        endforeach()
    endif()
    if(_winsdk_include AND EXISTS "${_winsdk_include}/cppwinrt")
        message(STATUS "Using Windows SDK WinRT headers at ${_winsdk_include}")
    else()
        message(WARNING "Windows SDK WinRT headers not found under ${_winsdk_root}; "
                        "WGC/OCR code may fail to compile.")
    endif()
endif()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "src/*.h")

add_executable(CODMSpawnSnack WIN32 ${SOURCES} ${HEADERS})

target_include_directories(CODMSpawnSnack PRIVATE src)
if(MSVC AND _winsdk_include AND EXISTS "${_winsdk_include}/cppwinrt")
    target_include_directories(CODMSpawnSnack PRIVATE
        "${_winsdk_include}/cppwinrt"
        "${_winsdk_include}/winrt"
        "${_winsdk_include}/um"
        "${_winsdk_include}/shared")
endif()

target_link_libraries(CODMSpawnSnack PRIVATE
    nlohmann_json::nlohmann_json
    ${OpenCV_LIBS}
    WindowsApp
    d3d11
    dxgi
    Ole32
    Dwmapi
    Shell32
    Shcore
    User32
    Gdi32
    Advapi32
)

target_compile_definitions(CODMSpawnSnack PRIVATE UNICODE _UNICODE NOMINMAX)

set_target_properties(CODMSpawnSnack PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
