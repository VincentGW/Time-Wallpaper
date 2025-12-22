@echo off
echo.
echo SFML Parallax Scroller Compiler
echo ===============================
echo.

if not exist main.cpp (
    echo Error: main.cpp not found!
    pause
    exit /b 1
)

echo Compiling SFML Parallax Scroller...
echo Using: C:\msys64\mingw64\bin\g++.exe
echo.

"C:\msys64\mingw64\bin\g++.exe" -std=c++17 main.cpp -o ParallaxScroller.exe -lsfml-graphics -lsfml-window -lsfml-system

if %errorlevel% equ 0 (
    echo.
    echo Build successful!
    echo Created: ParallaxScroller.exe
    echo.
    echo To run: ParallaxScroller.exe
    if exist ParallaxScroller.exe (
        echo Executable verified!
    )
) else (
    echo.
    echo Build failed!
    echo Make sure SFML is installed in MSYS2:
    echo   1. Open MSYS2 terminal
    echo   2. Run: pacman -S mingw-w64-x86_64-sfml
)

echo.
pause