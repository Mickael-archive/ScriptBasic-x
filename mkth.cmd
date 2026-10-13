@echo off
REM
REM This batch file builds the ScriptBasic documents.  It is the Windows side of
REM the documentation driver; mkth.sh is the UNIX one and does the same thing
REM with the same tools.
REM
REM The argument is the name of a document of texi/ without the .texi.jam
REM extension, or all for every document of the tree.  The result is
REM bin\texi\<name>.texi and the html pages of it next to that file, where the
REM makefiles of the modules put the mod_*.texi pages as well.
REM
REM The C tools of tools/codegen are used, so no Perl is needed: bin\tools\jamal
REM writes the texi out of the texi.jam source and bin\tools\t2h converts the
REM texi to html.  Run setup first, so that the two exist.
REM
REM The two documents of the regression suite, regtest and samplebas, want the
REM tmp/ subdirectory of the document as their current directory: they run
REM scriba with its error output into a file that the document then includes.
REM They are run there, with bin\exe on the PATH, and the directory is removed
REM again.
REM
REM usage: mkth <name> [<name> ...]
REM        mkth all
REM        mkth --texi <name>     convert an already built bin\texi\<name>.texi
REM
setlocal

if "%~1"=="" goto usage

set "top=%~dp0"
set "jamal=%top%bin\tools\jamal.exe"
set "t2h=%top%bin\tools\t2h.exe"
set "out=%top%bin\texi"

if not exist "%jamal%" goto nojamal
if not exist "%t2h%" goto not2h
if not exist "%out%" mkdir "%out%"

set "ERRCOUNT=0"

if /i "%~1"=="--texi" goto texionly

if /i not "%~1"=="all" goto named
set "names=esd auxfiles devguide docudoc eszterengine prep_dbg regtest samplebas source ug"
goto run
:named
set "names=%*"
:run
for %%n in (%names%) do call :one %%n
goto done

:texionly
if "%~2"=="" goto usage
set "name=%~2"
call :convonly
goto done

:one
set "name=%~1"
echo processing %name%
set "where=%top%texi"
set "src=%top%texi\%name%.texi.jam"
if /i "%name%"=="esd" set "where=%top:~0,-1%"
if /i "%name%"=="esd" set "src=%top%esd.texi.jam"
if not exist "%src%" goto nodoc

REM the command reference chapter of the User Guide is commands.texi, made out
REM of the documentation blocks of the commands/ sources by mkcdoc
if /i "%name%"=="ug" call :mkcdoc

call :warnmissing

call :build
goto :eof

:nodoc
echo ###ERROR %name%: no such document "%src%"
set /a ERRCOUNT+=1
goto :eof

:build
if /i "%name%"=="regtest" goto samples
if /i "%name%"=="samplebas" goto samples
pushd "%where%"
"%jamal%" "%name%.texi.jam" "%out%\%name%.texi"
set "rc=%ERRORLEVEL%"
popd
if not "%rc%"=="0" goto jamalfail
call :convert
goto :eof

:samples
if not exist "%top%bin\exe\scriba.exe" call :noscriba
set "scratch=%where%\tmp"
if not exist "%scratch%" mkdir "%scratch%"
pushd "%scratch%"
set "PATH=%top%bin\exe;%PATH%"
"%jamal%" "..\%name%.texi.jam" "%out%\%name%.texi" < NUL
set "rc=%ERRORLEVEL%"
popd
rmdir /s /q "%scratch%" 2>nul
if not "%rc%"=="0" goto jamalfail
call :convert
goto :eof

:jamalfail
echo ###ERROR %name%: jamal failed with status %rc%
set /a ERRCOUNT+=1
goto :eof

:convert
"%t2h%" "%out%\%name%.texi" "%out%\%name%"
if errorlevel 1 goto t2hfail
goto :eof

:convonly
if not exist "%out%\%name%.texi" goto notexi
echo converting %name%
"%t2h%" "%out%\%name%.texi" "%out%\%name%"
if errorlevel 1 goto t2hfail
goto :eof

:notexi
echo ###ERROR %name%: "%out%\%name%.texi" does not exist
set /a ERRCOUNT+=1
goto :eof

:t2hfail
echo ###ERROR %name%: t2h failed
set /a ERRCOUNT+=1
goto :eof

:mkcdoc
if not exist "%top%bin\tools\mkcdoc.exe" goto nomkcdoc
pushd "%top:~0,-1%"
bin\tools\mkcdoc.exe --texi commands.texi
popd
goto :eof

:nomkcdoc
echo ###WARNING ug: bin\tools\mkcdoc.exe is missing, run setup first.
goto :eof

:warnmissing
if /i not "%name%"=="auxfiles" goto wm1
if exist "%top%install.sh" goto wm1
echo ###WARNING auxfiles: install.sh is missing, so its listing comes out empty.
echo ###WARNING auxfiles: setup --install creates it.
:wm1
if /i not "%name%"=="auxfiles" goto wm2
if exist "%top%mkdeb.sh" goto wm2
echo ###WARNING auxfiles: mkdeb.sh is missing, so its listing comes out empty.
echo ###WARNING auxfiles: setup --install creates it.
:wm2
if /i not "%name%"=="eszterengine" goto :eof
if exist "%top%etc-init.d-sbhttpd" goto :eof
echo ###WARNING eszterengine: etc-init.d-sbhttpd is missing, so its listing comes out empty.
echo ###WARNING eszterengine: setup --install creates it.
goto :eof

:noscriba
echo ###WARNING %name%: bin\exe\scriba.exe is missing, so the sample programs
echo ###WARNING %name%: cannot be run and their output comes out empty.
echo ###WARNING %name%: run setup first.
goto :eof

:nojamal
echo %~nx0: "%jamal%" does not exist, run setup first
exit /b 1

:not2h
echo %~nx0: "%t2h%" does not exist, run setup first
exit /b 1

:usage
echo Usage: %~nx0 docname [docname ...]
echo        %~nx0 all
echo        %~nx0 --texi docname
exit /b 1

:done
if not "%ERRCOUNT%"=="0" goto errors
exit /b 0
:errors
echo there were %ERRCOUNT% errors
exit /b 1
