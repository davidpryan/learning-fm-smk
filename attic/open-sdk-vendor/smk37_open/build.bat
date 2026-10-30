@echo off
rem SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
setlocal
set "SMK37_MODEL=pro"
if /i "%~1"=="pro" goto pro
if /i "%~1"=="elite" goto elite
goto arguments
:elite
set "SMK37_MODEL=elite"
:pro
shift
:arguments
if "%~1"=="" goto build
if /i "%~1"=="--help" goto help
if /i "%~1"=="--web" goto web
if /i not "%~1"=="--package" goto invalid
if not "%~2"=="" goto invalid
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0sdk\Build.ps1" -Model %SMK37_MODEL% -Package
exit /b %errorlevel%
:build
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0sdk\Build.ps1" -Model %SMK37_MODEL%
exit /b %errorlevel%
:web
if not "%~2"=="" goto invalid
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Build-Web.ps1"
exit /b %errorlevel%
:invalid
echo Use build.bat [pro^|elite] [--web^|--package].
exit /b 2
:help
echo build.bat pro          Build Pro firmware
echo build.bat elite        Build Elite firmware
echo build.bat --web        Build browser preview
echo build.bat pro --package Build firmware and workbench
echo Toolchain and output paths: README.md
exit /b 0
