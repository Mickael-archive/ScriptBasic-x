@echo off
REM
REM Builds the template file of the SIBawa example with the C tools:
REM bin\tools\jamal writes templates.txt out of templates.txt.jam and
REM bin\tools\ssplit splits it into the files of the Templates directory.
REM
..\..\bin\tools\jamal.exe templates.txt.jam templates.txt
..\..\bin\tools\ssplit.exe templates.txt Templates
