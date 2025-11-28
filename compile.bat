@echo off
echo Compiling TimeWallpaper v3.0 - SFML Edition...

del TimeWallpaper.exe 2>nul

"C:\msys64\mingw64\bin\g++.exe" -std=c++17 main.cpp -o TimeWallpaper.exe -mwindows -lsfml-graphics -lsfml-window -lsfml-system -lwininet -luser32

if %ERRORLEVEL% EQU 0 (
    if exist TimeWallpaper.exe (
        echo.
        echo ✅ SUCCESS! TimeWallpaper.exe created.
        echo.
        echo TimeWallpaper v3.0 - Features:
        echo   • Fullscreen SFML overlay (no slow wallpaper API calls)
        echo   • Automatic location detection via IP geolocation
        echo   • Real astronomical data for your location
        echo   • Real-time color updates based on sun position
        echo   • Transparent watermark support
        echo   • Automatic daily solar time refresh
        echo   • Wake-from-sleep detection
        echo   • Configuration file with backup coordinates
        echo.
        echo Usage:
        echo   TimeWallpaper.exe       - Run fullscreen overlay
        echo   Press ESC to exit
        echo.
        echo Location auto-detected! Edit config.ini to customize or set backup coordinates.
        echo.
        set /p run="Run TimeWallpaper.exe now? (y/n): "
        if /i "%run%"=="y" (
            echo.
            TimeWallpaper.exe
        )
    )
) else (
    echo.
    echo ❌ FAILED to compile.
    echo Make sure g++ is available and libraries are accessible.
    echo Check errors above for details.
)

pause