@echo off
REM
REM The setup program of ScriptBasic under Windows.  The C setup program is
REM built by build\codegen\make into bin\tools\setup.exe (see Makefile.nt.jam).
REM
REM The source tree is one level up: setup reads its files there.
pushd "%~dp0.."
bin\tools\setup.exe --nt %*
popd
