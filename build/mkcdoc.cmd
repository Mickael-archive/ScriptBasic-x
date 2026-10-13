REM
REM This file lives in the build directory; the source tree is one level up.
REM
REM this batch file creates the command reference of the User Guide
REM and the HTML pages of the command reference
REM
pushd "%~dp0.."
bin\tools\mkcdoc
popd
