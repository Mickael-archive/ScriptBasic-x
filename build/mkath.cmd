@echo off
REM
REM Builds every document of the tree through mkth.cmd, and then the html of
REM every module page that make has written into bin\texi as mod_*.texi.
REM
REM Run make all first, so that those module pages exist.
REM
call "%~dp0mkth.cmd" all
for %%f in ("%~dp0..\bin\texi\mod_*.texi") do if exist "%%~ff" call "%~dp0mkth.cmd" --texi %%~nf
