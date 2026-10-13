REM This file lives in the build directory; the source tree is one level up.
pushd "%~dp0.."
bin\tools\generrh
bin\tools\headerer src\errcodes.c
popd
