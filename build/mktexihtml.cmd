@echo off
REM
REM Builds the documents the old harness called the texi to html documents.
REM mkth.cmd knows every document of the tree, so this is a call with the four
REM names.
REM
call "%~dp0mkth.cmd" devguide ug auxfiles source
