@echo off
REM
REM This command file compiles the ScriptBasic web content from html\source.txt.
REM It has to be executed from the source root directory.
REM
REM Both steps are the C tools of tools/codegen now, so no Perl is needed:
REM bin\tools\ssplit splits source.txt into the html\*.jam files its file lines
REM name -- one of them is the macro package macros.jim -- and then every one of
REM them is processed by bin\tools\jamal with that macro package.  The split
REM files are removed at the end.
REM
REM The output goes to the html directory next to the source root, which is
REM where the original used to write the pages of the site.
REM
cd html
..\bin\tools\ssplit.exe source.txt
for %%f in (*.jam) do if exist "%%f" ..\bin\tools\jamal.exe -m macros.jim %%f ..\..\html\%%~nf
del *.jam
del *.jim
cd ..
