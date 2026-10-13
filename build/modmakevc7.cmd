REM This file lives in the build directory; the source tree is one level up.
pushd "%~dp0.."
cd extensions\%1
nmake -f makefile.vc7
cd ..\..
popd
