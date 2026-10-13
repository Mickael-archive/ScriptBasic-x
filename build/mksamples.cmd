@echo off
REM
REM Builds one of the two documents of the regression suite, which show a sample
REM program and then the output of running it.  mkth.cmd gives those two the
REM tmp/ subdirectory they want and puts bin\exe on the PATH, so this is a call
REM with the name of the document.
REM
REM usage: mksamples regtest
REM        mksamples samplebas
REM
call "%~dp0mkth.cmd" %*
