# Build instructions

> **Prerequisite:** a Windows 10/11 machine with **Visual Studio 2022** and the **Windows SDK**. This project uses WGC (Windows Graphics Capture) and WinRT OCR, which require the Windows SDK and MSVC.

## 1. Install the toolchain

1. Install [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/) (Community edition is fine).
2. In the installer, select:
   - **Desktop development with C++**
   - **Windows 10/11 SDK** (10.0.19041.0 or newer)
   - **C++/WinRT** (optional but recommended; see step 3)
3. Install [vcpkg](https://vcpkg.io/) and add it to `PATH` / `VCPKG_ROOT`.
4. Install [CMake](https://cmake.org/download/) (3.20+). CMake ships with Visual Studio, so you can also use the IDE generator directly.

## 2. C++/WinRT projection headers

This project consumes `Windows.Graphics.Capture` and `Windows.Media.Ocr` through C++/WinRT projection headers (`winrt/...`).

### Option A: C++/WinRT VSIX (recommended)

Install the **Microsoft.Windows.CppWinRT** VSIX from the Visual Studio Marketplace, or install the `Microsoft.Windows.CppWinRT` NuGet package. The CMake project will use the headers if `WINRT_INCLUDE_DIR` is set to the directory containing `winrt/Windows.Graphics.Capture.h`.

### Option B: Generate from the Windows SDK

Run the `cppwinrt.exe` tool that comes with the Windows SDK to generate headers into a local directory, then pass that directory to CMake:

```powershell
# Adjust SDK version and architecture as needed
& "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\cppwinrt.exe" `
    -in "C:\Program Files (x86)\Windows Kits\10\References\10.0.19041.0" `
    -out .\generated_winrt
```

Then configure CMake with `-DWINRT_INCLUDE_DIR=.\generated_winrt`.

## 3. Clone and configure

```powershell
git clone https://github.com/Andymaster007/CODMSpawnSnack.git
cd CODMSpawnSnack
copy config.example.json config.json
# Edit config.json to match your emulator window title and calibration data.
```

## 4. Build with CMake

```powershell
# From the project root, using vcpkg manifest mode
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
cmake --build build --config Release
```

Or open the folder directly in Visual Studio 2022 (it will detect CMake and vcpkg automatically).

The compiled binary will be at `build/bin/CODMSpawnSnack.exe`.

## 5. Run

1. Start your CODM emulator and enter a Bomb Mode match.
2. Run `CODMSpawnSnack.exe`.
3. Right-click the tray icon → **Calibrate** to capture the HUD template and adjust ROIs.
4. Click **Start**.

## Troubleshooting

- **No capture / black frame**: make sure the emulator window is visible (not minimized) and not in full-screen-exclusive mode. WGC cannot capture minimized windows.
- **OCR fails**: confirm the Windows 10/11 OCR language pack for Chinese is installed (`设置 > 时间和语言 > 语言和区域 > 中文语言包`).
- **Cannot set foreground**: run the tool as a normal user; do **not** run as Administrator, because that can break `SetForegroundWindow` rules.
