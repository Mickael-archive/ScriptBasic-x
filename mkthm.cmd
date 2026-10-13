@echo off
REM
REM Converts the texi page of a module to html.  The texi itself is written by
REM the makefile of the module when make is run in its directory, and that
REM makefile uses the same C tools; this only runs bin\tools\t2h on the page
REM make has left in bin\texi.
REM
REM usage: mkthm mod_hash
REM
call "%~dp0mkth.cmd" --texi %*
