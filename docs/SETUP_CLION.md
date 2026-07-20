# 环境搭建教程（CLion + MSVC + vcpkg）

> 目标：在 Windows 上用 **CLion** 编译本项目（C++20，依赖 OpenCV / nlohmann-json，使用 Windows.Graphics.Capture 与 Windows.Media.Ocr）。
> 本教程假设你用的是 **Visual Studio Build Tools 2022**（不是完整 VS IDE），配合 CLion 写代码。

---

## 1. 安装 VS Build Tools 2022

1. 下载地址：<https://visualstudio.microsoft.com/downloads/> → 拉到 **“Tools for Visual Studio 2022”** → 点 **“Build Tools for Visual Studio 2022”** 的下载按钮，得到 `vs_BuildTools.exe`。
2. 运行安装器，在 **工作负荷（Workloads）** 里勾选：
   - ✅ **使用 C++ 的桌面开发**（Desktop development with C++）
3. 在右侧 **单个组件（Individual components）** 里确认以下被选中（默认一般会勾，但建议核对）：
   - ✅ **MSVC v143 - VS 2022 C++ x64/x86 生成工具**（版本号选最新的）
   - ✅ **Windows 10 SDK** 或 **Windows 11 SDK**，版本 **≥ 10.0.18362**（WGC API 要求 1903 / build 18362+，任意较新 SDK 都满足；默认勾选的就是最新的，直接用即可）
   - ✅ **C++ CMake 工具 for Windows**
   - ✅ **适用于 v143 生成工具的 C++/CLI 支持**（可选）
4. 点 **安装**，等完成（约 5–15 分钟，取决于网速）。

> **关于 Windows SDK**：不必单独去微软官网下 SDK 安装包，“使用 C++ 的桌面开发” 已经把 SDK 带进来了。
> **关于 C++/WinRT**：本项目只 **消费** 现有 WinRT API（不写自定义 `.idl`），所以不需要 cppwinrt 编译器。WinRT 的头文件在 SDK 的 `Include\<ver>\cppwinrt` 目录里，本项目 CMake 会手动加这个 include 路径（见第 5 步），无需额外安装。

---

## 2. 验证 MSVC 可用

打开 **“x64 Native Tools Command Prompt for VS 2022”**（开始菜单搜 `x64` 就能找到），输入：

```bat
cl
```

能打印出版本信息（如 `Microsoft (R) C/C++ Optimizing Compiler Version 19.x`）即成功。

---

## 3. 在 CLion 里配置 MSVC 工具链

1. 打开 CLion → **Settings / Preferences**（`Ctrl+Alt+S`）。
2. 进入 **Build, Execution, Deployment → Toolchains**。
3. 点 `+` → 选 **Visual Studio**（或 **MSVC**）。CLion 会自动探测到刚装的 Build Tools 2022。
   - **Architecture** 选 `amd64`（x86_64）。
   - **Toolset** 一般自动填好（指向 Build Tools 的 MSVC）。
   - **CMake** 选 Bundled 或系统装的都行。
   - **Debugger** 选 Visual Studio 的 `lldb`/`cdb`（CLion 会提示安装 Windows 调试工具，按提示装即可）。
4. 看到 `C Compiler = cl.exe`、`C++ Compiler = cl.exe` 都检测到，点 **Apply**。

> CLion 加载 MSVC 工具链时会自动运行 `vcvars`，因此 `WindowsSdkDir`、`WindowsSDKVersion` 等环境变量在构建时是可用的（本项目 CMake 依赖它们定位 WinRT 头文件）。

---

## 4. 安装并配置 vcpkg

1. 在一个**不含中文/空格**的目录（例如 `C:\vcpkg` 或 `D:\vcpkg`）打开终端：

   ```bat
   git clone https://github.com/microsoft/vcpkg.git
   cd vcpkg
   .\bootstrap-vcpkg.bat
   ```

2. 设置环境变量（系统级，便于 CLion 和 CMake 找到它）：
   - 变量名：`VCPKG_ROOT`
   - 变量值：你的 vcpkg 根目录，例如 `C:\vcpkg`
   - 建议同时把 `%VCPKG_ROOT%` 加到 `PATH`（可选）。
   - 设置后**重启 CLion** 让其读到新环境变量。

3. 本项目根目录已有 `vcpkg.json`（声明了 `opencv`、`nlohmann-json`）。开启 **manifest 模式** 后，CMake 配置时会自动下载并编译这些依赖（首次编译 OpenCV 较慢，可能 10–30 分钟，属正常）。

---

## 5. 把 vcpkg + WinRT 接进 CLion 的 CMake

在 CLion 里：

1. **Settings → Build, Execution, Deployment → CMake**
2. 选中你的 CMake Profile（默认 `Debug` / `Release`），在 **CMake options** 填入：

   ```
   -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
   ```

3. **Apply** 后 CLion 会重新配置（Reload CMake Project），此时 vcpkg 开始拉取/编译 OpenCV。

`CMakeLists.txt` 里还需要手动把 WinRT 头文件路径加进来（本项目已内置，这里说明原理）：

```cmake
if(MSVC)
  # VS 工具链已通过 vcvars 设置 WindowsSdkDir / WindowsSDKVersion
  set(WINSDK_INCLUDE "$ENV{WindowsSdkDir}Include/$ENV{WindowsSDKVersion}")
  target_include_directories(${PROJECT_NAME} PRIVATE
    "${WINSDK_INCLUDE}/cppwinrt"   # C++/WinRT 投影头文件（Windows.Graphics.Capture 等）
    "${WINSDK_INCLUDE}/winrt"
    "${WINSDK_INCLUDE}/um"
    "${WINSDK_INCLUDE}/shared")
  target_link_libraries(${PROJECT_NAME} PRIVATE windowsapp.lib)  # WinRT API 的伞形导入库
endif()
```

> **关键坑（CLion × WinRT）**：本项目只消费 WinRT，不定义新的 WinRT 组件，因此**不需要** `cppwinrt.exe` 去编译 `.idl`。只要 include SDK 自带的 `cppwinrt` 头目录 + 链接 `windowsapp.lib` 即可。这绕开了 CLion 对 C++/WinRT IDL 编译支持较弱的老问题。

---

## 6. 编译运行

1. CLion 里 **Build → Build Project**（或 `Ctrl+F9`）。
2. 首次会因为 vcpkg 编译 OpenCV 等依赖较慢，请耐心等待。
3. 编译通过后，配置 **Run/Debug Configuration**，把 `config.json` 放到可执行文件同目录，运行即可。

---

## 7. 常见问题

- **CLion 找不到 MSVC**：确认第 1 步装的是 “使用 C++ 的桌面开发” 工作负荷，且重启了 CLion。
- **`windowsapp.lib` 链接报错 / 找不到 WinRT 头**：确认 `WindowsSdkDir` 环境变量在构建期存在（CLion MSVC 工具链默认会注入）；检查 CMake 里 include 路径拼接是否正确。
- **vcpkg 依赖编译卡住**：首次编译 OpenCV 确实久，是正常的；可临时把 `vcpkg.json` 里不需要的端口去掉以加速。
- **WGC 抓不到窗口**：确认被捕获的模拟器窗口**没有最小化**（WGC 无法捕获最小化窗口）；窗口需处于后台但非最小化状态。

---

## 8. 一键核对清单

- [ ] 装了 VS Build Tools 2022 + “使用 C++ 的桌面开发”
- [ ] Windows SDK ≥ 10.0.18362（默认勾选的最新版即可）
- [ ] CLion 里检测到 MSVC (cl.exe) 工具链
- [ ] 装了 vcpkg，设置了 `VCPKG_ROOT` 并重启 CLion
- [ ] CMake options 里填了 vcpkg toolchain 路径
- [ ] `CMakeLists.txt` 已加 WinRT include 路径 + `windowsapp.lib`
- [ ] 首次 Build 完成（等 OpenCV 编译完）
