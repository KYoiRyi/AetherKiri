#Requires -Version 5.1
<#
.SYNOPSIS
    KrKr2-Next Android 一键编译脚本 (Windows)

.DESCRIPTION
    在 Windows 平台上完整编译 KrKr2-Next 项目，生成可在 Android 设备运行的 APK。

    编译流程:
      1. 检测必要工具 (Git, CMake, Ninja, MSVC, JDK, Flutter, Android SDK/NDK)
      2. 初始化 vcpkg 并安装 Android 依赖库
      3. 调用 Flutter 构建 Android APK（自动触发 Gradle + CMake 原生编译）
      4. 验证并输出 APK 路径

.PARAMETER BuildType
    编译类型: debug 或 release (默认: debug)

.PARAMETER Abi
    目标 ABI，逗号分隔 (默认: arm64-v8a)
    可选值: arm64-v8a, armeabi-v7a, x86_64, x86

.PARAMETER Jobs
    并行编译线程数 (默认: CPU核心数)

.PARAMETER Clean
    编译前清理旧的构建缓存

.PARAMETER AndroidHome
    Android SDK 目录 (若已设置 $env:ANDROID_HOME 则自动读取)

.PARAMETER AndroidNdkHome
    Android NDK 目录 (若已设置 $env:ANDROID_NDK_HOME 则自动读取)

.PARAMETER VcpkgRoot
    vcpkg 根目录 (默认自动在 .devtools/vcpkg 目录初始化)

.PARAMETER FlutterSdk
    Flutter SDK 根目录 (默认从 PATH 自动检测)

.PARAMETER LogFile
    日志文件路径 (默认: <项目根目录>/build_android.log)

.EXAMPLE
    .\build_android_windows.ps1
.EXAMPLE
    .\build_android_windows.ps1 -BuildType release -Clean
.EXAMPLE
    .\build_android_windows.ps1 -Abi "arm64-v8a,x86_64" -BuildType release
.EXAMPLE
    .\build_android_windows.ps1 -AndroidHome "C:\Android\Sdk" -AndroidNdkHome "C:\Android\Sdk\ndk\27.0.12077973"
#>

[CmdletBinding()]
param(
    [ValidateSet("debug", "release", "Debug", "Release")]
    [string]$BuildType = "debug",

    [string]$Abi = "arm64-v8a",

    [int]$Jobs = [System.Environment]::ProcessorCount,

    [switch]$Clean,

    [string]$AndroidHome = "",
    [string]$AndroidNdkHome = "",
    [string]$VcpkgRoot = "",
    [string]$FlutterSdk = "",
    [string]$LogFile = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ════════════════════════════════════════════════════════════════
#  日志系统
# ════════════════════════════════════════════════════════════════
$script:BuildStartTime = Get-Date
$script:StepTimings    = [System.Collections.Generic.List[hashtable]]::new()
$script:CurrentStep    = ""
$script:StepStart      = $null

# 日志文件路径
$ProjectRoot = $PSScriptRoot
if (-not $LogFile) { $LogFile = Join-Path $ProjectRoot "build_android.log" }

# 创建/清空日志文件，写入头部
"" | Set-Content $LogFile -Encoding UTF8
function Write-Log {
    param([string]$Msg)
    $ts = (Get-Date).ToString("HH:mm:ss")
    Add-Content -Path $LogFile -Value "[$ts] $Msg" -Encoding UTF8
}

function Elapsed {
    param([datetime]$Since)
    $span = (Get-Date) - $Since
    if ($span.TotalSeconds -lt 60) { return "$([int]$span.TotalSeconds)s" }
    return "$([int]$span.TotalMinutes)m $($span.Seconds)s"
}

# ────── 输出函数 ──────
function Write-Step {
    param([string]$Msg)
    $script:CurrentStep = $Msg
    $script:StepStart   = Get-Date
    $line = "=" * 62
    Write-Host ""
    Write-Host $line -ForegroundColor Cyan
    Write-Host "  $Msg" -ForegroundColor Cyan
    Write-Host $line -ForegroundColor Cyan
    Write-Host ""
    Write-Log ""
    Write-Log $line
    Write-Log "  $Msg"
    Write-Log $line
}

function Write-SubStep {
    param([string]$Msg)
    Write-Host "  ▶ $Msg" -ForegroundColor DarkCyan
    Write-Log "  >> $Msg"
}

function Write-Info {
    param([string]$Msg)
    $ts = (Get-Date).ToString("HH:mm:ss")
    Write-Host "  [$ts] $Msg" -ForegroundColor Green
    Write-Log "  [INFO] $Msg"
}

function Write-Detail {
    param([string]$Msg)
    Write-Host "         $Msg" -ForegroundColor DarkGray
    Write-Log "         $Msg"
}

function Write-Warn {
    param([string]$Msg)
    $ts = (Get-Date).ToString("HH:mm:ss")
    Write-Host "  [$ts] [WARN] $Msg" -ForegroundColor Yellow
    Write-Log "  [WARN] $Msg"
}

function Write-Ok {
    param([string]$Msg)
    Write-Host "  ✔ $Msg" -ForegroundColor Green
    Write-Log "  [OK] $Msg"
}

function Write-Fail {
    param([string]$Msg)
    $ts = (Get-Date).ToString("HH:mm:ss")
    Write-Host ""
    Write-Host "  [$ts] [ERROR] $Msg" -ForegroundColor Red
    Write-Log   "  [ERROR] $Msg"
    Write-Host ""
    Write-Host "  ► 完整日志已保存至: $LogFile" -ForegroundColor Yellow

    # 记录失败步骤耗时
    if ($script:StepStart) {
        $elapsed = Elapsed $script:StepStart
        $script:StepTimings.Add(@{ Step = $script:CurrentStep; Status = "FAILED"; Elapsed = $elapsed })
    }
    Write-SummaryTable
    exit 1
}

function Complete-Step {
    # 记录当前步骤完成耗时
    if ($script:StepStart) {
        $elapsed = Elapsed $script:StepStart
        $script:StepTimings.Add(@{ Step = $script:CurrentStep; Status = "OK"; Elapsed = $elapsed })
        Write-Info "└─ 完成 (耗时 $elapsed)"
    }
}

function Write-SummaryTable {
    Write-Host ""
    Write-Host "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" -ForegroundColor DarkGray
    Write-Host "  编译阶段汇总" -ForegroundColor White
    Write-Host "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" -ForegroundColor DarkGray
    foreach ($entry in $script:StepTimings) {
        $icon   = if ($entry.Status -eq "OK") { "✔" } else { "✘" }
        $color  = if ($entry.Status -eq "OK") { "Green" } else { "Red" }
        $name   = $entry.Step.PadRight(42)
        Write-Host "  $icon  $name $($entry.Elapsed)" -ForegroundColor $color
    }
    $totalElapsed = Elapsed $script:BuildStartTime
    Write-Host "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" -ForegroundColor DarkGray
    Write-Host "  总计耗时: $totalElapsed" -ForegroundColor White
    Write-Host ""
}

# ════════════════════════════════════════════════════════════════
#  辅助函数
# ════════════════════════════════════════════════════════════════
function Find-Command {
    param([string]$Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Assert-Command {
    param([string]$Name, [string]$HintUrl = "")
    Write-SubStep "检测 $Name ..."
    $path = Find-Command $Name
    if (-not $path) {
        $hint = if ($HintUrl) { "`n       下载地址: $HintUrl" } else { "" }
        Write-Fail "'$Name' 未找到，请安装后将其加入 PATH。$hint"
    }
    Write-Detail "路径: $path"
}

# ════════════════════════════════════════════════════════════════
#  启动日志头
# ════════════════════════════════════════════════════════════════
$DevtoolsDir   = Join-Path $ProjectRoot ".devtools"
$FlutterAppDir = Join-Path $ProjectRoot "apps\flutter_app"
$BuildTypeLower = $BuildType.ToLower()
$BuildTypeCap   = (Get-Culture).TextInfo.ToTitleCase($BuildTypeLower)

Write-Log "================================================================"
Write-Log "  KrKr2-Next Android Build  |  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
Write-Log "================================================================"
Write-Log "  BuildType : $BuildTypeCap"
Write-Log "  ABI       : $Abi"
Write-Log "  Jobs      : $Jobs"
Write-Log "  Clean     : $($Clean.IsPresent)"
Write-Log "  ProjectRoot: $ProjectRoot"
Write-Log "================================================================"

Write-Host ""
Write-Host "  ╔══════════════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "  ║         KrKr2-Next  Android  Build  (Windows)           ║" -ForegroundColor Cyan
Write-Host "  ╚══════════════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""
Write-Host "  日志文件 : $LogFile" -ForegroundColor DarkGray
Write-Host "  开始时间 : $($script:BuildStartTime.ToString('HH:mm:ss'))" -ForegroundColor DarkGray
Write-Host ""
Write-Host "  配置摘要:" -ForegroundColor White
Write-Host "    编译类型  : $BuildTypeCap" -ForegroundColor Gray
Write-Host "    目标 ABI  : $Abi" -ForegroundColor Gray
Write-Host "    并行线程  : $Jobs" -ForegroundColor Gray
Write-Host "    清理缓存  : $($Clean.IsPresent)" -ForegroundColor Gray
Write-Host "    项目目录  : $ProjectRoot" -ForegroundColor Gray
Write-Host ""

# ════════════════════════════════════════════════════════════════
#  步骤 1: 前置工具检测
# ════════════════════════════════════════════════════════════════
Write-Step "步骤 1/4 — 前置工具检测"

# ── Git ──────────────────────────────────────────────────────
Assert-Command "git" "https://git-scm.com/download/win"
$gitVer = (git --version 2>&1).ToString().Trim()
Write-Ok "Git     : $gitVer"

# ── CMake ────────────────────────────────────────────────────
Assert-Command "cmake" "https://cmake.org/download/"
$cmakeVer = (cmake --version 2>&1 | Select-String "cmake version").ToString().Trim()
Write-Ok "CMake   : $cmakeVer"

# ── Ninja ────────────────────────────────────────────────────
Assert-Command "ninja" "https://github.com/ninja-build/ninja/releases"
$ninjaVer = (ninja --version 2>&1).ToString().Trim()
Write-Ok "Ninja   : v$ninjaVer"

# ── Visual Studio / MSVC ─────────────────────────────────────
Write-SubStep "检测 Visual Studio / MSVC Build Tools ..."
Write-Detail  "vcpkg 在 Windows 编译 host 工具时必须使用 MSVC cl.exe"

$vswherePaths = @(
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
    "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
)
$vswhereExe = $vswherePaths | Where-Object { Test-Path $_ } | Select-Object -First 1
$script:VcvarsPath = $null

if ($vswhereExe) {
    Write-Detail "vswhere 路径: $vswhereExe"
    $vsInstallPath = & $vswhereExe -latest -products "*" `
        -requires "Microsoft.VisualStudio.Component.VC.Tools.x86.x64" `
        -property installationPath 2>$null
    if ($vsInstallPath -and (Test-Path $vsInstallPath)) {
        $vcvars = Join-Path $vsInstallPath "VC\Auxiliary\Build\vcvars64.bat"
        if (Test-Path $vcvars) {
            $script:VcvarsPath = $vcvars
            Write-Detail "VS 安装目录: $vsInstallPath"
        }
    }
} else {
    Write-Detail "vswhere.exe 未找到，尝试硬编码路径回退..."
}

if (-not $script:VcvarsPath) {
    $fallbacks = @(
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    )
    foreach ($fb in $fallbacks) {
        Write-Detail "检查: $fb"
        if (Test-Path $fb) { $script:VcvarsPath = $fb; Write-Detail "找到!"; break }
    }
}

if (-not $script:VcvarsPath) {
    Write-Host ""
    Write-Host "  ╔══════════════════════════════════════════════════════════════╗" -ForegroundColor Red
    Write-Host "  ║  缺少 Visual Studio C++ 编译工具 (MSVC)                      ║" -ForegroundColor Red
    Write-Host "  ╠══════════════════════════════════════════════════════════════╣" -ForegroundColor Red
    Write-Host "  ║  vcpkg 在 Windows 上编译 host 工具时必须使用 MSVC。           ║" -ForegroundColor Red
    Write-Host "  ║                                                              ║" -ForegroundColor Red
    Write-Host "  ║  解决方法（二选一）:                                          ║" -ForegroundColor Red
    Write-Host "  ║                                                              ║" -ForegroundColor Red
    Write-Host "  ║  ① Visual Studio Build Tools 2022 (免费，推荐):              ║" -ForegroundColor Yellow
    Write-Host "  ║    https://aka.ms/vs/17/release/vs_BuildTools.exe            ║" -ForegroundColor Yellow
    Write-Host "  ║    → 勾选工作负载: [使用 C++ 的桌面开发]                      ║" -ForegroundColor Yellow
    Write-Host "  ║                                                              ║" -ForegroundColor Red
    Write-Host "  ║  ② Visual Studio 2022 Community (免费完整 IDE):              ║" -ForegroundColor Yellow
    Write-Host "  ║    https://visualstudio.microsoft.com/downloads/             ║" -ForegroundColor Yellow
    Write-Host "  ║    → 勾选工作负载: [使用 C++ 的桌面开发]                      ║" -ForegroundColor Yellow
    Write-Host "  ║                                                              ║" -ForegroundColor Red
    Write-Host "  ║  安装完成后重新运行本脚本即可。                                ║" -ForegroundColor Red
    Write-Host "  ╚══════════════════════════════════════════════════════════════╝" -ForegroundColor Red
    Write-Host ""
    Write-Fail "未找到 MSVC，请安装 Visual Studio Build Tools 2022 后重试。"
}
Write-Ok "MSVC    : $($script:VcvarsPath)"

# ── Java / JDK ───────────────────────────────────────────────
Write-SubStep "检测 Java (JDK 17+) ..."
$javaPath = Find-Command "java"
if (-not $javaPath) {
    Write-Fail "未找到 Java。请安装 JDK 17+。`n       下载地址: https://adoptium.net/"
}
Write-Detail "java 路径: $javaPath"
$javaVerRaw = (java -version 2>&1 | Select-Object -First 1).ToString()
Write-Ok "Java    : $javaVerRaw"

# 检测版本号 >= 17
$javaVerMatch = [regex]::Match($javaVerRaw, '(\d+)[._]')
if ($javaVerMatch.Success) {
    $javaVerNum = [int]$javaVerMatch.Groups[1].Value
    if ($javaVerNum -lt 17) {
        Write-Warn "检测到 Java $javaVerNum，推荐 JDK 17+，可能导致 Gradle 构建失败。"
    } else {
        Write-Detail "Java 版本 $javaVerNum >= 17 ✔"
    }
}

# ── Flutter ──────────────────────────────────────────────────
Write-SubStep "检测 Flutter SDK ..."

if ($FlutterSdk -and (Test-Path "$FlutterSdk\bin\flutter.bat")) {
    $FlutterBin = "$FlutterSdk\bin\flutter.bat"
    Write-Detail "使用参数指定路径: $FlutterSdk"
} elseif (Test-Path (Join-Path $ProjectRoot ".devtools\flutter\bin\flutter.bat")) {
    $FlutterSdk = Join-Path $ProjectRoot ".devtools\flutter"
    $FlutterBin = "$FlutterSdk\bin\flutter.bat"
    Write-Detail "使用 .devtools/flutter: $FlutterSdk"
} else {
    $flutterCmd = Find-Command "flutter"
    if ($flutterCmd) {
        $FlutterBin = $flutterCmd
        $FlutterSdk = Split-Path (Split-Path $flutterCmd -Parent) -Parent
        Write-Detail "从 PATH 检测到: $flutterCmd"
    } else {
        Write-Fail "未找到 Flutter SDK。`n       请安装: https://docs.flutter.dev/get-started/install/windows`n       或放置于: $ProjectRoot\.devtools\flutter"
    }
}

# 如果 Flutter 路径包含空格，使用 Junction (目录链接) 将其挂载到没有空格的安全位置
if ($FlutterSdk -match '\s') {
    $flutterSafeRoot = Join-Path $env:LOCALAPPDATA "KrKr2-Next-flutter-safe"
    if (-not (Test-Path $flutterSafeRoot)) {
        Write-Detail "检测到 Flutter SDK 路径含有空格，正为其创建无空格的安全挂载点 (Junction) -> $flutterSafeRoot"
        New-Item -ItemType Junction -Path $flutterSafeRoot -Value $FlutterSdk | Out-Null
    } else {
        Write-Detail "复用已存在的安全挂载点 (Junction): $flutterSafeRoot"
    }
    $FlutterSdk = $flutterSafeRoot
    $FlutterBin = Join-Path $FlutterSdk "bin\flutter.bat"
}

Write-Detail "SDK 根目录: $FlutterSdk"
Write-Detail "flutter 可执行: $FlutterBin"
$flutterVerOutput = (& $FlutterBin --version 2>&1) -join " "
Write-Ok "Flutter : $($flutterVerOutput.Substring(0, [Math]::Min(80, $flutterVerOutput.Length)))"

# ── Android SDK ──────────────────────────────────────────────
Write-SubStep "检测 Android SDK ..."

if (-not $AndroidHome) {
    $sources = @(
        @{ Desc = "环境变量 ANDROID_HOME";    Value = $env:ANDROID_HOME },
        @{ Desc = "环境变量 ANDROID_SDK_ROOT"; Value = $env:ANDROID_SDK_ROOT }
    )
    foreach ($src in $sources) {
        if ($src.Value -and (Test-Path $src.Value)) {
            $AndroidHome = $src.Value
            Write-Detail "来源: $($src.Desc) = $($src.Value)"
            break
        } elseif ($src.Value) {
            Write-Detail "跳过 $($src.Desc) (路径不存在): $($src.Value)"
        }
    }
    if (-not $AndroidHome) {
        $candidates = @(
            "$env:LOCALAPPDATA\Android\Sdk",
            "$env:USERPROFILE\AppData\Local\Android\Sdk",
            "C:\Android\Sdk"
        )
        foreach ($c in $candidates) {
            Write-Detail "检查默认路径: $c"
            if (Test-Path $c) { $AndroidHome = $c; Write-Detail "找到!"; break }
        }
    }
}

if (-not $AndroidHome -or -not (Test-Path $AndroidHome)) {
    Write-Fail "未找到 Android SDK。`n       请安装 Android Studio 或设置 `$env:ANDROID_HOME。`n       下载: https://developer.android.com/studio"
}
Write-Ok "Android SDK : $AndroidHome"

# 检测 SDK 中关键工具
$adbPath = Join-Path $AndroidHome "platform-tools\adb.exe"
if (Test-Path $adbPath) {
    $adbVer = (& $adbPath version 2>&1 | Select-Object -First 1).ToString().Trim()
    Write-Detail "adb: $adbVer"
} else {
    Write-Warn "adb.exe 未找到，安装后才能一键安装 APK 到设备。"
}

# ── Android NDK ──────────────────────────────────────────────
Write-SubStep "检测 Android NDK ..."

if (-not $AndroidNdkHome) {
    $ndkSources = @(
        @{ Desc = "ANDROID_NDK_HOME"; Value = $env:ANDROID_NDK_HOME },
        @{ Desc = "ANDROID_NDK";      Value = $env:ANDROID_NDK }
    )
    foreach ($src in $ndkSources) {
        if ($src.Value -and (Test-Path $src.Value)) {
            $AndroidNdkHome = $src.Value
            Write-Detail "来源: $($src.Desc) = $($src.Value)"
            break
        } elseif ($src.Value) {
            Write-Detail "跳过 $($src.Desc) (路径不存在): $($src.Value)"
        }
    }
    if (-not $AndroidNdkHome) {
        $ndkBaseDir = Join-Path $AndroidHome "ndk"
        if (Test-Path $ndkBaseDir) {
            Write-Detail "扫描 NDK 目录: $ndkBaseDir"
            $latestNdk = Get-ChildItem $ndkBaseDir -Directory |
                Sort-Object { [System.Version]($_.Name -replace '[^0-9.]','') } -Descending |
                Select-Object -First 1
            if ($latestNdk) {
                $AndroidNdkHome = $latestNdk.FullName
                Write-Detail "选取最新版本: $($latestNdk.Name)"
            }
        }
        if (-not $AndroidNdkHome) {
            $ndkBundle = Join-Path $AndroidHome "ndk-bundle"
            if (Test-Path $ndkBundle) {
                $AndroidNdkHome = $ndkBundle
                Write-Detail "使用 ndk-bundle (旧版布局)"
            }
        }
    }
}

if (-not $AndroidNdkHome -or -not (Test-Path $AndroidNdkHome)) {
    Write-Fail "未找到 Android NDK。`n       请在 Android Studio → SDK Manager → SDK Tools 中安装 'NDK (Side by side)'`n       或设置: `$env:ANDROID_NDK_HOME = 'C:\Android\Sdk\ndk\<version>'"
}
# 读取 NDK 版本号
$ndkSrcProps = Join-Path $AndroidNdkHome "source.properties"
$ndkVersion = "unknown"
if (Test-Path $ndkSrcProps) {
    $prop = Get-Content $ndkSrcProps | Where-Object { $_ -match "Pkg.Revision" }
    if ($prop) {
        $ndkVersion = ($prop -split "=")[1].Trim()
    }
}
Write-Ok "Android NDK : $AndroidNdkHome (Version: $ndkVersion)"

$env:ANDROID_NDK_HOME = $AndroidNdkHome
$env:ANDROID_NDK_VERSION = $ndkVersion

Complete-Step

# ════════════════════════════════════════════════════════════════
#  步骤 2: 初始化 vcpkg 并安装 Android 依赖
# ════════════════════════════════════════════════════════════════
Write-Step "步骤 2/4 — 初始化 vcpkg 并安装 Android 依赖"

# ── 确定 vcpkg 根目录 ────────────────────────────────────────
Write-SubStep "定位 vcpkg ..."

if ($env:VCPKG_ROOT -and (Test-Path "$env:VCPKG_ROOT\.vcpkg-root")) {
    $VcpkgRoot = $env:VCPKG_ROOT
} else {
    $VcpkgRoot = Join-Path $DevtoolsDir "vcpkg"
}

if ($VcpkgRoot -match '\s' -or $ProjectRoot -match '\s') {
    $safeDirName = (Split-Path $ProjectRoot -Leaf) + "-vcpkg-safe"
    $VcpkgRoot   = Join-Path $env:LOCALAPPDATA $safeDirName
    Write-Warn "检测到终端环境变量或系统路径存在空格，为彻底防止 MSYS2 环境崩溃，"
    Write-Warn "已强制将 vcpkg 工作区转入纯净目录: $VcpkgRoot"
}

if (-not (Test-Path "$VcpkgRoot\.vcpkg-root")) {
    Write-Warn "在此无空格目录未检测到 vcpkg，正在自动纯净克隆 (目标: $VcpkgRoot) ..."
    New-Item -ItemType Directory -Force -Path (Split-Path $VcpkgRoot) | Out-Null
    git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot 2>&1 | Tee-Object -Append $LogFile
    if ($LASTEXITCODE -ne 0) { Write-Fail "自动克隆 vcpkg 失败" }
    Write-Detail "纯净克隆完成: $VcpkgRoot"
} else {
    Write-Detail "使用无空格安全目录 vcpkg: $VcpkgRoot"
}
Write-Ok "vcpkg 根目录: $VcpkgRoot"

# ── 确保 vcpkg.exe 存在 ──────────────────────────────────────
$VcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"
if (-not (Test-Path $VcpkgExe)) {
    Write-SubStep "bootstrap vcpkg (首次需要 MSVC 环境)..."
    $bootstrapScript = Join-Path $VcpkgRoot "bootstrap-vcpkg.bat"
    if (-not (Test-Path $bootstrapScript)) {
        Write-Fail "bootstrap-vcpkg.bat 未找到于 $VcpkgRoot"
    }
    Write-Detail "执行: $bootstrapScript -disableMetrics"
    $cmdBoot = "`"$($script:VcvarsPath)`" && cd /d `"$VcpkgRoot`" && bootstrap-vcpkg.bat -disableMetrics"
    cmd /c $cmdBoot 2>&1 | Tee-Object -Append $LogFile
    if ($LASTEXITCODE -ne 0) { Write-Fail "vcpkg bootstrap 失败" }
    Write-Ok "vcpkg bootstrap 完成"
} else {
    Write-Detail "vcpkg.exe 已存在，跳过 bootstrap"
}

$vcpkgVerLine = (& $VcpkgExe version 2>&1 | Select-Object -First 1).ToString().Trim()
Write-Ok "vcpkg   : $vcpkgVerLine"
Write-Detail "可执行路径: $VcpkgExe"

# ── 设置环境变量 ─────────────────────────────────────────────
$env:VCPKG_ROOT       = $VcpkgRoot
$env:ANDROID_HOME     = $AndroidHome
$env:ANDROID_NDK_HOME = $AndroidNdkHome
$env:VCPKG_BUILD_TYPE = "release"
Write-Detail "已设置 VCPKG_ROOT=$VcpkgRoot"
Write-Detail "已设置 ANDROID_HOME=$AndroidHome"
Write-Detail "已设置 ANDROID_NDK_HOME=$AndroidNdkHome"
Write-Detail "已设置 VCPKG_BUILD_TYPE=release (仅编译 Release，节省一半耗时)"

# ── overlay 路径 ─────────────────────────────────────────────
$OverlayPorts    = Join-Path $ProjectRoot "vcpkg\ports"
$OverlayTripletsOriginal = Join-Path $ProjectRoot "vcpkg\triplets"
$OverlayTriplets = $OverlayTripletsOriginal

# 动态拦截并只编译 Release，节约一半以上的时间
$TempOverlayTriplets = Join-Path $DevtoolsDir "temp_triplets"
if (Test-Path $TempOverlayTriplets) { Remove-Item -Recurse -Force $TempOverlayTriplets }
New-Item -ItemType Directory -Force -Path $TempOverlayTriplets | Out-Null

if (Test-Path $OverlayTripletsOriginal) {
    Copy-Item -Path "$OverlayTripletsOriginal\*" -Destination $TempOverlayTriplets -Recurse -Force
}

# 强制为所有的本地 triplet 加上 release 配置
Get-ChildItem $TempOverlayTriplets -Filter "*.cmake" | ForEach-Object {
    Add-Content -Path $_.FullName -Value "`nset(VCPKG_BUILD_TYPE release)`n" -Encoding UTF8
}

# 同时强制宿主工具 (x64-windows) 也只编译 Release
$hostTripletPath = Join-Path $TempOverlayTriplets "x64-windows.cmake"
$defaultHostTriplet = (Join-Path $VcpkgRoot "triplets\x64-windows.cmake").Replace('\', '/')
$hostTripletContent = @"
include("$defaultHostTriplet")
set(VCPKG_BUILD_TYPE release)
"@
Set-Content -Path $hostTripletPath -Value $hostTripletContent -Encoding UTF8

$OverlayTriplets = $TempOverlayTriplets
Write-Detail "已生成强制 Release 重写规则的临时 Triplet 目录: $OverlayTriplets"

if (Test-Path $OverlayPorts) {
    $portCount = @(Get-ChildItem $OverlayPorts -Directory).Count
    Write-Detail "overlay-ports: $OverlayPorts ($portCount 个自定义 port)"
} else {
    Write-Detail "overlay-ports: 未找到 $OverlayPorts，跳过"
}
if (Test-Path $OverlayTriplets) {
    $tripletFiles = @(Get-ChildItem $OverlayTriplets -Filter "*.cmake").Count
    Write-Detail "overlay-triplets: $OverlayTriplets ($tripletFiles 个自定义 triplet)"
} else {
    Write-Detail "overlay-triplets: 未找到 $OverlayTriplets，跳过"
}

# ── ABI → triplet 映射 ───────────────────────────────────────
function Get-Triplet {
    param([string]$AbiName)
    switch ($AbiName.Trim()) {
        "arm64-v8a"   { return "arm64-android" }
        "armeabi-v7a" { return "arm-android" }
        "x86_64"      { return "x64-android" }
        "x86"         { return "x86-android" }
        default       { Write-Fail "未知的 ABI: $AbiName" }
    }
}

# ── vcpkg 调用包装器（通过 vcvars64.bat 激活 MSVC 环境）──────
function Invoke-VcpkgWithMsvc {
    param([string[]]$VcpkgArgs)
    $argsStr = ($VcpkgArgs | ForEach-Object {
        if ($_ -match ' ') { "`"$_`"" } else { $_ }
    }) -join " "
    
    # 强制在 cmd 内合并 stderr 到 stdout 2>&1，避免 PowerShell 错误流拦截导致严重缓冲
    $cmdLine = "`"$($script:VcvarsPath)`" && cd /d `"$ProjectRoot`" && `"$VcpkgExe`" $argsStr 2>&1"
    Write-Detail "CMD: $cmdLine"
    
    cmd.exe /c $cmdLine | ForEach-Object {
        Write-Host $_
        Add-Content -Path $LogFile -Value $_ -Encoding UTF8
    }
    return $LASTEXITCODE
}

# ── 逐 ABI 安装依赖 ──────────────────────────────────────────
$AbiArray = $Abi -split "," | ForEach-Object { $_.Trim() } | Where-Object { $_ }

foreach ($abi in $AbiArray) {
    $triplet = Get-Triplet $abi
    Write-SubStep "安装 vcpkg 依赖 — triplet: $triplet (ABI: $abi)"

    # 检查 ANGLE 脏缓存（已知会导致重新构建问题）
    $angleBuildtrees = Join-Path $VcpkgRoot "buildtrees\angle"
    $anglePkg        = Join-Path $VcpkgRoot "packages\angle_$triplet"
    if (Test-Path $angleBuildtrees) {
        Write-Warn "检测到旧的 ANGLE 构建缓存，将清理以确保 overlay port 生效..."
        Remove-Item -Recurse -Force $angleBuildtrees
        Write-Detail "已删除: $angleBuildtrees"
    }
    if (Test-Path $anglePkg) {
        Remove-Item -Recurse -Force $anglePkg
        Write-Detail "已删除: $anglePkg"
    }

    $vcpkgArgs = @(
        "install",
        "--triplet", $triplet,
        "--x-install-root=$VcpkgRoot\installed",
        "--x-manifest-root=$ProjectRoot"
    )
    if (Test-Path $OverlayPorts)    { $vcpkgArgs += "--overlay-ports=$OverlayPorts" }
    if (Test-Path $OverlayTriplets) { $vcpkgArgs += "--overlay-triplets=$OverlayTriplets" }

    Write-Detail "vcpkg 参数: $($vcpkgArgs -join ' ')"
    Write-Detail "注意: 首次执行将从源码编译所有依赖，可能耗时 1-2 小时。"
    Write-Host ""
    Write-Host "  ┌────────────────────────────────────────────────────────────" -ForegroundColor DarkGray
    Write-Host "  │  vcpkg install 输出 (实时)" -ForegroundColor DarkGray
    Write-Host "  └────────────────────────────────────────────────────────────" -ForegroundColor DarkGray

    $vcpkgStart = Get-Date
    $exitCode   = Invoke-VcpkgWithMsvc -VcpkgArgs $vcpkgArgs

    $vcpkgElapsed = Elapsed $vcpkgStart
    Write-Host ""
    if ($exitCode -ne 0) {
        Write-Fail "vcpkg install 失败 (triplet: $triplet, 耗时 $vcpkgElapsed)。`n       请检查 $LogFile 获取完整错误日志。"
    }
    Write-Ok "vcpkg 依赖安装完成: $triplet  (耗时 $vcpkgElapsed)"

    # 验证安装结果
    $installedDir = Join-Path $VcpkgRoot "installed\$triplet"
    if (Test-Path $installedDir) {
        $libCount = @(Get-ChildItem "$installedDir\lib" -Filter "*.a" -ErrorAction SilentlyContinue).Count +
                    @(Get-ChildItem "$installedDir\lib" -Filter "*.so" -ErrorAction SilentlyContinue).Count
        Write-Detail "已安装库文件数: $libCount (.a / .so)"
    } else {
        Write-Warn "installed/$triplet 目录未找到，安装结果可能不完整。"
    }
}

Complete-Step

# ════════════════════════════════════════════════════════════════
#  步骤 3: 构建 Flutter Android APK
# ════════════════════════════════════════════════════════════════
Write-Step "步骤 3/4 — 构建 Flutter Android APK ($BuildTypeCap)"

# ── 写入 local.properties ────────────────────────────────────
Write-SubStep "写入 android/local.properties ..."
$localPropsPath    = Join-Path $FlutterAppDir "android\local.properties"
$sdkDirEscaped     = $AndroidHome.Replace("\", "\\")
$ndkDirEscaped     = $AndroidNdkHome.Replace("\", "\\")
$flutterSdkEscaped = $FlutterSdk.Replace("\", "\\")
$localPropsContent = @"
sdk.dir=$sdkDirEscaped
ndk.dir=$ndkDirEscaped
flutter.sdk=$flutterSdkEscaped
"@
Set-Content -Path $localPropsPath -Value $localPropsContent -Encoding UTF8
Write-Detail "路径: $localPropsPath"
Write-Detail "sdk.dir=$sdkDirEscaped"
Write-Detail "ndk.dir=$ndkDirEscaped"
Write-Detail "flutter.sdk=$flutterSdkEscaped"
Write-Ok "local.properties 已写入"

# ── 清理旧缓存（可选）────────────────────────────────────────
if ($Clean) {
    Write-SubStep "清理旧的构建缓存 (-Clean 已指定) ..."
    $cleanPaths = @(
        "$FlutterAppDir\build\app",
        "$FlutterAppDir\build\.cxx",
        "$FlutterAppDir\.dart_tool"
    )
    foreach ($p in $cleanPaths) {
        if (Test-Path $p) {
            $size = [math]::Round((Get-ChildItem $p -Recurse -File -ErrorAction SilentlyContinue |
                Measure-Object -Property Length -Sum).Sum / 1MB, 1)
            Remove-Item -Recurse -Force $p
            Write-Detail "已删除: $p  (${size} MB)"
        } else {
            Write-Detail "跳过 (不存在): $p"
        }
    }
    Write-Ok "缓存清理完成"
}

# ── flutter pub get ──────────────────────────────────────────
Write-SubStep "flutter pub get — 同步 Dart 依赖 ..."
Write-Detail "工作目录: $FlutterAppDir"
Push-Location $FlutterAppDir
$pubStart = Get-Date
cmd.exe /c "`"$FlutterBin`" pub get 2>&1" | ForEach-Object {
    Write-Host $_
    Add-Content -Path $LogFile -Value $_ -Encoding UTF8
}
if ($LASTEXITCODE -ne 0) { Pop-Location; Write-Fail "flutter pub get 失败" }
Write-Ok "flutter pub get 完成 (耗时 $(Elapsed $pubStart))"

# ── 构建 ABI → Flutter platform 映射 ────────────────────────
$flutterPlatforms = @()
foreach ($abi in $AbiArray) {
    switch ($abi) {
        "arm64-v8a"   { $flutterPlatforms += "android-arm64" }
        "armeabi-v7a" { $flutterPlatforms += "android-arm" }
        "x86_64"      { $flutterPlatforms += "android-x64" }
        "x86"         { $flutterPlatforms += "android-x86" }
    }
}
$targetPlatform = $flutterPlatforms -join ","

# ── flutter build apk ────────────────────────────────────────
Write-SubStep "flutter build apk ..."
Write-Detail "目标平台  : $targetPlatform"
Write-Detail "编译类型  : $BuildTypeLower"
Write-Detail "flutter 路径: $FlutterBin"
Write-Detail "注意: 此步骤包含 Gradle 下载、CMake 交叉编译，首次可能耗时 30-60 分钟。"
Write-Host ""
Write-Host "  ┌────────────────────────────────────────────────────────────" -ForegroundColor DarkGray
Write-Host "  │  flutter build apk 输出 (实时)" -ForegroundColor DarkGray
Write-Host "  └────────────────────────────────────────────────────────────" -ForegroundColor DarkGray

$flutterBuildStart = Get-Date
$flutterBuildArgsStr = "build apk --$BuildTypeLower --target-platform $targetPlatform"
$cmdLine = "`"$FlutterBin`" $flutterBuildArgsStr 2>&1"
Write-Detail "CMD: $cmdLine"

cmd.exe /c $cmdLine | ForEach-Object {
    Write-Host $_
    Add-Content -Path $LogFile -Value $_ -Encoding UTF8
}
$buildExitCode = $LASTEXITCODE
Pop-Location

Write-Host ""
if ($buildExitCode -ne 0) {
    Write-Fail "flutter build apk 失败 (耗时 $(Elapsed $flutterBuildStart))。`n       详情: $LogFile"
}
Write-Ok "flutter build apk 完成 (耗时 $(Elapsed $flutterBuildStart))"

Complete-Step

# ════════════════════════════════════════════════════════════════
#  步骤 4: 验证输出 APK
# ════════════════════════════════════════════════════════════════
Write-Step "步骤 4/4 — 验证输出 APK"

Write-SubStep "查找 APK 文件 ..."
$apkExpected = Join-Path $FlutterAppDir "build\app\outputs\flutter-apk\app-$BuildTypeLower.apk"
Write-Detail "预期路径: $apkExpected"

$apkPath = $null
if (Test-Path $apkExpected) {
    $apkPath = $apkExpected
    Write-Detail "已找到预期路径"
} else {
    Write-Warn "预期路径未找到，扫描 outputs 目录..."
    $found = Get-ChildItem "$FlutterAppDir\build\app\outputs" -Recurse -Filter "*.apk" `
        -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) {
        $apkPath = $found.FullName
        Write-Detail "找到备用路径: $apkPath"
    }
}

if (-not $apkPath -or -not (Test-Path $apkPath)) {
    Write-Fail "APK 文件未找到！构建可能已失败。`n       请检查日志: $LogFile"
}

$apkItem   = Get-Item $apkPath
$apkSizeMB = [math]::Round($apkItem.Length / 1MB, 2)
$apkDate   = $apkItem.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss")
Write-Ok "APK 文件已生成"
Write-Detail "路径: $apkPath"
Write-Detail "大小: ${apkSizeMB} MB"
Write-Detail "时间: $apkDate"

# ── 检查 APK 内的原生库 ──────────────────────────────────────
Write-SubStep "检查 APK 内容（原生库）..."
$nativeLibs  = @()

if (Find-Command "unzip") {
    $nativeLibs = (unzip -l "$apkPath" 2>$null | Select-String "\.so$" | ForEach-Object {
        ($_ -split '\s+')[-1]
    }) -ne ""
} elseif (Find-Command "7z") {
    $nativeLibs = (7z l "$apkPath" 2>$null | Select-String "\.so$" | ForEach-Object {
        ($_ -split '\s+')[-1]
    }) -ne ""
} elseif ([System.IO.Compression.ZipFile]) {
    try {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zip = [System.IO.Compression.ZipFile]::OpenRead($apkPath)
        $nativeLibs = $zip.Entries | Where-Object { $_.FullName -match "\.so$" } |
            Select-Object -ExpandProperty FullName
        $zip.Dispose()
    } catch { Write-Detail "ZIP 读取失败，跳过内容检查" }
}

if ($nativeLibs.Count -gt 0) {
    Write-Ok "APK 中包含 $($nativeLibs.Count) 个原生库 (.so)"
    foreach ($lib in $nativeLibs) {
        Write-Detail "  ✔ $lib"
    }
} else {
    Write-Warn "未能列出 APK 内原生库（可能缺少 unzip/7z 工具），请手动验证。"
}

Complete-Step

# ════════════════════════════════════════════════════════════════
#  编译完成 — 汇总报告
# ════════════════════════════════════════════════════════════════
Write-SummaryTable

Write-Host ""
Write-Host "  ╔══════════════════════════════════════════════════════════════╗" -ForegroundColor Green
Write-Host "  ║                    编 译 成 功  ✔                           ║" -ForegroundColor Green
Write-Host "  ╠══════════════════════════════════════════════════════════════╣" -ForegroundColor Green
Write-Host "  ║  APK : $($apkPath.PadRight(53))║" -ForegroundColor Green
Write-Host "  ║  大小: ${apkSizeMB} MB$(" " * (50 - "$apkSizeMB MB".Length))║" -ForegroundColor Green
Write-Host "  ╚══════════════════════════════════════════════════════════════╝" -ForegroundColor Green
Write-Host ""
Write-Host "  安装到已连接设备:" -ForegroundColor Cyan
Write-Host "    adb install `"$apkPath`"" -ForegroundColor White
Write-Host ""
Write-Host "  安装并启动:" -ForegroundColor Cyan
Write-Host "    adb install `"$apkPath`" && adb shell am start -n org.github.krkr2.flutter_app/.MainActivity" -ForegroundColor White
Write-Host ""
Write-Host "  完整日志: $LogFile" -ForegroundColor DarkGray
Write-Host ""

Write-Log ""
Write-Log "================================================================"
Write-Log "  BUILD SUCCESS  |  APK: $apkPath  |  Size: ${apkSizeMB} MB"
Write-Log "  结束时间: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
Write-Log "  总耗时: $(Elapsed $script:BuildStartTime)"
Write-Log "================================================================"
