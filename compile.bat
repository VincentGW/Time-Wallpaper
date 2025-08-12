@echo off
echo Compiling TimeWallpaper v2.0 - Solar Edition...

g++ -std=c++17 main.cpp -o TimeWallpaper.exe -lgdi32 -luser32 -lwininet

if exist TimeWallpaper.exe (
    echo.
    echo ✅ SUCCESS! TimeWallpaper.exe created.
    echo.
    echo TimeWallpaper v2.0 - Features:
    echo   • Automatic location detection via IP geolocation
    echo   • Real astronomical data for your location
    echo   • Minute-by-minute wallpaper updates
    echo   • Automatic daily solar time refresh
    echo   • Timeout-resistant API calls with retry
    echo   • Configuration file with backup coordinates
    echo   • Continuous service mode
    echo.
    echo Usage:
    echo   TimeWallpaper.exe       - Run continuously
    echo.
    echo Location auto-detected! Edit config.ini to customize or set backup coordinates.
    echo.
    set /p run="Run TimeWallpaper.exe now? (y/n): "
    if /i "%run%"=="y" (
        echo.
        TimeWallpaper.exe
    )
) else (
    echo.
    echo ❌ FAILED to compile.
    echo Make sure g++ is available and libraries are accessible.
)

pause