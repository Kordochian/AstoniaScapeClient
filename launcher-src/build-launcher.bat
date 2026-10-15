@echo off
rem Builds AstoniaLauncher.exe into the client folder with the C# compiler that ships with Windows.
cd /d "%~dp0"
set CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe
if not exist "%CSC%" set CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe
"%CSC%" /nologo /target:winexe /optimize+ /win32icon:..\res\moa3.ico /r:System.Security.dll /out:..\AstoniaLauncher.exe AstoniaLauncher.cs
