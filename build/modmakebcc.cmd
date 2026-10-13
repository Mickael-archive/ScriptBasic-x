REM This file lives in the build directory; the source tree is one level up.
pushd "%~dp0.."
cd extensions\%1
make -f makefile.bcc
cd ..\..
popd
