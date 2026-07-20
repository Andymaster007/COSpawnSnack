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
# Option A: install the Microsoft.Windows.CppWinRT VSIX / NuGet and set WINRT_INCLUDE_DIR.
# Option B: run cppwinrt.exe from the Windows SDK to generate the headers.
find_program(CPPWINRT_TOOL cppwinrt.exe HINTS
    "$ENV{WindowsSdkVerBinPath}/$ENV{PROCESSOR_ARCHITECTURE}"
    "$ENV{WindowsSdkVerBinPath}/x64"
    "$ENV{WindowsSdkVerBinPath}/x86"
    "C:/Program Files (x86)/Windows Kits/10/bin/10.0.19041.0/x64"
)
if(CPPWINRT_TOOL)
    message(STATUS "Found cppwinrt.exe: ${CPPWINRT_TOOL}")
else()
    message(STATUS "cppwinrt.exe not found; assuming pre-generated C++/WinRT headers are available.")
endif()

set(WINRT_INCLUDE_DIR "" CACHE PATH
    "Path to C++/WinRT generated headers (directory containing winrt/Windows.Graphics.Capture.h)")

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "src/*.h")

add_executable(CODMSpawnSnack WIN32 ${SOURCES} ${HEADERS})

target_include_directories(CODMSpawnSnack PRIVATE src)
if(WINRT_INCLUDE_DIR)
    target_include_directories(CODMSpawnSnack PRIVATE ${WINRT_INCLUDE_DIR})
endif()

target_link_libraries(CODMSpawnSnack PRIVATE
    nlohmann_json::nlohmann_json
    ${OpenCV_LIBS}
    WindowsApp
    Ole32
    Dwmapi
    Shell32
    Shcore
    User32
    Gdi32
    Advapi32
)

target_compile_definitions(CODMSpawnSnack PRIVATE UNICODE _UNICODE)

set_target_properties(CODMSpawnSnack PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
