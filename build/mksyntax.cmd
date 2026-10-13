@echo off
REM This file lives in the build directory; the source tree is one level up.
pushd "%~dp0.."
echo Creating syntax.c from the source file syntax.def.
echo Syntax.c contains the tables that hold the syntax
echo definition of the BASIC commands.
bin\tools\syntaxer
popd
