# WebView2 在本机失效 · 管理员取证清单

> 目标：确定 `msedgewebview2.exe` 起不来，到底是
> - **(A) WDAC / 代码完整性策略「按路径放行」漏了 WebView2 运行时**（→ 加白名单可修），还是
> - **(B) WebView2 运行时自身损坏 / 缺依赖**（→ 加白名单无效，需重装）。
>
> 为什么需要管理员：WDAC 策略与 CodeIntegrity 事件日志的读取都需要提权。
> 所有命令都在**以管理员身份**打开的 PowerShell / CMD 里执行。

---

## 步骤 0 — 已知环境（已确认，可跳过）

- 系统：**Windows 11 build 26200**
- `msedgewebview2.exe --version` → **exit 13**（进程在初始化前就死，零输出）
- `msedge.exe --version` → **exit 0**（正常）
- 两进程同版（150.0.4078.83）、同微软签名
- ⇒ 已基本排除「一刀切只放行微软签名」的 WDAC（那样两进程应同生共死）。指向 **路径规则** 或 **运行时损坏**。

---

## 步骤 1 — 抓 CodeIntegrity 拦截事件【决定性】

这是最快区分 A / B 的办法。**WDAC 一旦拦截进程，必然在操作日志留事件**；若没有事件却依然 exit 13，那就是运行时自身的问题。

1. 管理员 PowerShell，先触发一次运行时启动（会秒退，正常）：
   ```powershell
   & "C:\Program Files (x86)\Microsoft\EdgeWebView\Application\150.0.4078.83\msedgewebview2.exe" --version
   ```
2. 立刻读 CodeIntegrity 操作日志里最近的拦截事件：
   ```powershell
   Get-WinEvent -LogName "Microsoft-Windows-CodeIntegrity/Operational" -MaxEvents 50 |
     Where-Object { $_.Id -in @(3033, 3034, 3029, 3076, 3077, 3089) } |
     Select-Object TimeCreated, Id, Message | Format-List
   # 更稳妥：干脆不过滤 Id，直接看最近 10 条原始事件（最容易抓到 3033/3089 伴侣对）：
   #   Get-WinEvent -LogName "Microsoft-Windows-CodeIntegrity/Operational" -MaxEvents 10 | Format-List TimeCreated, Id, Message
   ```
   或用 wevtutil（CMD 也可）：
   ```
   wevtutil qe "Microsoft-Windows-CodeIntegrity/Operational" /c:50 /rd:true /f:text
   ```
3. **判读**：
   - 看到 **事件 3076 且 Message 点名 `msedgewebview2.exe`** → 确定是 **WDAC 拦截（情况 A）**，事件里会带 policy ID / 规则信息。→ 走步骤 2 + 修复方案 A。
   - 看到 **事件 3033**（*“…attempted to load `<某 DLL>` that did not meet the Microsoft signing level requirements”*）且**被加载进程是 `msedgewebview2.exe`** → 这是 **HVCI / 内存完整性（Core Isolation）拦下了某个试图注入进该进程的第三方 DLL**（情况 C，游戏本/主板音频/灯光类软件最常见，如 Nahimic/A-Volute 的 `AudioDevProps2.dll`、Sonic Studio、Dolby、RivaTuner 等）。**运行时本身没坏、也不是 exe 路径规则**。→ 走修复方案 C（卸载/禁用该注入软件），不要重装运行时、也不要给 exe 加白名单。
   - **既没有 3076 也没有 3033**，但进程仍 exit 13 → 才是运行时损坏（情况 B）。→ 走步骤 3 + 修复方案 B。

> 备注：3076 在「审计模式」下也会记录但放行；在「强制模式」下既记录又杀进程。无论哪种，只要出现就说明 WDAC 在管它。

---

## 步骤 2 — 看 WDAC 策略是否按路径放行（仅当步骤 1 指向 A）

1. 列出已部署策略（需管理员）：
   ```
   citool -lp
   ```
   若返回策略列表且状态为 Enforced / Blocking，记下 policy ID。
2. 看策略对 Edge 与 WebView2 的处理差异。把活动策略导出 XML 后搜两个路径：
   ```powershell
   # 活动策略通常存放处
   Get-ChildItem "C:\Windows\System32\CodeIntegrity\CiPolicies\Active\" -Recurse
   # 若有 ConfigCI 模块，可直接导出当前策略为 XML 后搜索：
   #   Import-Module ConfigCI
   #   Get-CIPolicy -FilePath <导出的xml>
   ```
   重点搜 `Microsoft\Edge\Application` 与 `Microsoft\EdgeWebView\Application`：
   - 若策略里有 `msedge.exe` 的 Allow / FileRule，但**没有** `msedgewebview2.exe` → 证实「路径放行漏了运行时」，加白名单能修。
3. 也可让系统直接判定某文件是否被允许（**部分 Win11 版本支持**，不支持就以步骤 1 的事件为准）：
   ```
   citool --verify-file "C:\Program Files (x86)\Microsoft\EdgeWebView\Application\150.0.4078.83\msedgewebview2.exe"
   ```

---

## 步骤 3 — 排除运行时损坏（当步骤 1 指向 B）

1. 校验签名（应为 `Valid`；若 `Invalid` 说明被篡改 / 损坏）：
   ```powershell
   (Get-AuthenticodeSignature "C:\Program Files (x86)\Microsoft\EdgeWebView\Application\150.0.4078.83\msedgewebview2.exe").Status
   ```
2. 看是否多版本并存导致加载错版本：
   ```
   Get-ChildItem "C:\Program Files (x86)\Microsoft\EdgeWebView\Application\" -Directory
   ```
3. 抓更详细的启动日志（看具体卡在哪一步）：
   ```
   "C:\Program Files (x86)\Microsoft\EdgeWebView\Application\150.0.4078.83\msedgewebview2.exe" --version --enable-logging=stderr --v=1
   ```
4. 修复：以管理员运行 WebView2 运行时安装器做 **repair**，或先卸载再重装：
   ```
   # Evergreen Bootstrapper 下载：https://go.microsoft.com/fwlink/p/?LinkId=2124703
   MicrosoftEdgeWebView2RuntimeInstallerX64.exe /repair
   ```

---

## 步骤 4 — 辅助排查（Smart App Control / HVCI）

- Smart App Control 状态（仅 Win11；它基于 WDAC，但只拦「未知发布者」程序，理论上不拦微软签名的 msedgewebview2）：
  ```powershell
  Get-MpComputerStatus | Select SmartAppControlState
  ```
- 内核隔离 / 内存完整性（HVCI）：`msinfo32` → 「基于虚拟化的安全」。HVCI 一般不影响 msedgewebview2，若开着且异常可临时关闭验证。

---

## 判读决策树

```
msedgewebview2 起不来，但 msedge 正常
        │
        ├─ 步骤1 抓到 3076 且点名 msedgewebview2
        │        → 情况 A：WDAC 路径规则漏了运行时
        │        → 修复 A：给 msedgewebview2.exe 加 FileRule（哈希或路径）到策略，重新部署
        │
        ├─ 步骤1 抓到 3033 且被加载进程是 msedgewebview2
        │        → 情况 C：HVCI / 内存完整性拦下某第三方注入 DLL（如 Nahimic/A-Volute）
        │        → 修复 C：卸载/禁用该注入软件（或临时关 Memory Integrity 验证）；不要重装运行时
        │
        └─ 步骤1 既无 3076 也无 3033，仍 exit 13
                 → 情况 B：运行时损坏 / 缺依赖
                 → 修复 B：签名若 Invalid 或 repair / reinstall 运行时；加白名单无效
```

---

## 修复方案速查

### A（加白名单，仅当确认是 WDAC）
在 CI 策略 XML 增加一条 Allow 规则（**建议用文件哈希最稳**）：
```xml
<FileRule ID="AllowWebView2Runtime" FriendlyName="Allow WebView2 Runtime">
  <FileHash Type="SHA256">...msedgewebview2.exe 的 SHA256 哈希...</FileHash>
</FileRule>
```
重新 `ConvertFrom-CIPolicy` + `citool --update-policy`（或走 Intune 部署）。
⚠️ **只修你这一台开发机，救不了分发出去的用户。**

### B（重装运行时）
管理员运行安装器 `/repair`，或卸载后重装 Evergreen Runtime。

### C（移除注入软件，HVCI 拦 DLL 时）
- 找到 3033 里被拦的 DLL 所属软件（如 `A-Volute\Nahimic` → Nahimic 音频；`RivaTuner` → 监控 overlay 等），**卸载**或在其实设置里关闭「注入到所有进程 / 空间音效」。
- 快速验证是否就是它：临时关闭 **内存完整性**（Windows 安全中心 → 设备安全性 → 内核隔离 → 内存完整性 关），重启后 `msedgewebview2.exe --version` 若恢复正常 → 确认是 HVCI + 该注入 DLL。验证完建议**恢复内存完整性**，并改为卸载该软件（而非长期关安全特性）。
- 注意：此情况**重装 WebView2 运行时无效**（运行时没坏），**给 msedgewebview2 加 WDAC 白名单也无效**（被拦的是注入的 DLL 不是 exe 本体）。

---

## 对你这个项目的意义

无论 A 还是 B，**都只影响你这一台开发机**。分发版应继续：
- **WebView2 主路径**：普通机器（默认无强制 WDAC）零问题，原 HTML UI 正常显示；
- **已实现的原生 Win32 兜底**：锁死的机器自动降级，两端用户都零下载。

不要为了单台机器抛弃 WebView2。取证清单只是为了让你自己的开发机能用回漂亮的 WebView2 UI（如果你在意的话）。
