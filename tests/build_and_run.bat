@echo off
setlocal
set ZIG_BIN="C:\Users\zxc\AppData\Local\Microsoft\WinGet\Packages\zig.zig_Microsoft.Winget.Source_8wekyb3d8bbwe\zig-x86_64-windows-0.16.0\zig.exe"

echo [*] Compiling test suite...
%ZIG_BIN% cc -O2 -o test_shaders.exe test_shaders.c -lopengl32 -lgdi32
if errorlevel 1 goto error
%ZIG_BIN% cc -O2 -o test_lanczos_math.exe test_lanczos_math.c
if errorlevel 1 goto error
%ZIG_BIN% cc -O2 -o test_dvfel_queue.exe test_dvfel_queue.c
if errorlevel 1 goto error
%ZIG_BIN% cc -O2 -o test_memory_viewport.exe test_memory_viewport.c
if errorlevel 1 goto error
%ZIG_BIN% cc -O2 -o test_suite.exe test_suite.c
if errorlevel 1 goto error

echo [*] Executing master test runner...
.\test_suite.exe
goto end

:error
echo [!] Compilation failed!
exit /b 1

:end
