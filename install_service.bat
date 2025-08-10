@echo off
echo Installing TimeWallpaper as Windows Service...
echo.

REM Check for admin privileges
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: This script requires administrator privileges.
    echo Right-click and select "Run as administrator"
    pause
    exit /b 1
)

REM Get current directory
set CURRENT_DIR=%cd%

echo Creating service wrapper...
echo @echo off > TimeWallpaperService.bat
echo cd /d "%CURRENT_DIR%" >> TimeWallpaperService.bat
echo TimeWallpaper.exe -c >> TimeWallpaperService.bat

echo Installing Windows service...
sc create "TimeWallpaper" binPath= "%CURRENT_DIR%\TimeWallpaperService.bat" start= auto DisplayName= "TimeWallpaper - Solar Edition"

if %errorlevel% equ 0 (
    echo.
    echo ✅ Service installed successfully!
    echo.
    echo To start the service:
    echo   net start TimeWallpaper
    echo.
    echo To stop the service:
    echo   net stop TimeWallpaper  
    echo.
    echo To uninstall the service:
    echo   sc delete TimeWallpaper
    echo.
    set /p start="Start the service now? (y/n): "
    if /i "%start%"=="y" (
        net start TimeWallpaper
    )
) else (
    echo.
    echo ❌ Failed to install service.
    echo Make sure you're running as administrator.
)

pause