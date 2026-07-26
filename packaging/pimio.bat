@echo off
rem pimio launcher.
rem
rem Runs the application no matter what the current working directory is. The
rem Qt deployment resolves its plugins through bin\qt.conf, whose Prefix is
rem relative to the executable, so the whole extracted tree must stay together
rem and the executable must be started from its own location.

setlocal
set "PIMIO_HOME=%~dp0"
set "PIMIO_EXE=%PIMIO_HOME%bin\pimio.exe"

if not exist "%PIMIO_EXE%" (
    echo pimio: cannot find the application binary at "%PIMIO_EXE%".>&2
    echo pimio: extract the whole archive and keep its directory layout intact.>&2
    exit /b 1
)

cd /d "%PIMIO_HOME%"
"%PIMIO_EXE%" %*
exit /b %ERRORLEVEL%
