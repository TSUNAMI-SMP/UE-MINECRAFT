@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 (
  echo Run this file from an x64 Native Tools Command Prompt for Visual Studio 2022.
  exit /b 1
)
if not defined JAVA_HOME (
  echo JAVA_HOME must point to the Java 21 JDK folder containing include\jni.h.
  exit /b 1
)
pushd "%~dp0"
cl /nologo /std:c++17 /O2 /EHsc /MT /LD /I. /I"%JAVA_HOME%\include" uebridge_gpu.cpp /link d3d11.lib dxgi.lib opengl32.lib /OUT:uebridge_gpu.dll
if errorlevel 1 (popd & exit /b 1)
echo Built %CD%\uebridge_gpu.dll
popd
