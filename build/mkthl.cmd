@echo off
REM
REM mkthl is the light version of mkth: it builds the texi of a document and the
REM html pages of it and nothing else, without the rtf, the ps, the pdf and the
REM chm that the full build used to add.
REM
REM The driver of this tree, mkth.cmd here and mkth.sh on UNIX, uses the C tools
REM bin\tools\jamal and bin\tools\t2h and runs neither Perl nor TeX, so the
REM light build and the full one are the same thing now.  This file is kept
REM because it is the name the documents and the old habits use.
REM
REM usage: mkthl <name> [<name> ...]   (the same as mkth)
REM
call "%~dp0mkth.cmd" %*
