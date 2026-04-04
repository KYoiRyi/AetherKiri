@echo off
setlocal

echo =======================================================
echo          KrKr2-Next Android Release Builder            
echo =======================================================
echo.

for /f "delims=" %%a in ('powershell -command "Get-Date -Format 'yyyyMMdd_HHmmss'"') do set TIMESTAMP=%%a

echo [1/3] Running build script (BuildType: release)...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_android_windows.ps1" -BuildType release

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Build process failed. Please check the logs.
    pause
    exit /b 1
)

echo.
echo [2/3] Build completed. Locating APK file...
set APK_SOURCE="%~dp0apps\flutter_app\build\app\outputs\flutter-apk\app-release.apk"
set APK_DEST="%~dp0KrKr2-Next_release_%TIMESTAMP%.apk"

if exist %APK_SOURCE% (
    echo [3/3] Copying and renaming APK to root directory...
    copy /Y %APK_SOURCE% %APK_DEST% >nul
    if %ERRORLEVEL% equ 0 (
        echo.
        echo =======================================================
        echo [SUCCESS] APK built and exported successfully!
        echo Location: %APK_DEST%
        echo =======================================================
    ) else (
        echo.
        echo [ERROR] Failed to copy the APK file.
    )
) else (
    echo.
    echo [ERROR] Could not find the generated APK file!
    echo Expected path: %APK_SOURCE%
)

echo.
pause
