bin\tools\jamal.exe -m make_vc7.jim extensions\%1\configure.cmd.jam extensions\%1\configure_vc7.cmd
bin\tools\jamal.exe -m make_bcc.jim extensions\%1\configure.cmd.jam extensions\%1\configure_bcc.cmd
bin\tools\jamal.exe -m make_vc7.jim extensions\%1\makefile.jam extensions\%1\makefile.vc7
bin\tools\jamal.exe -m make_bcc.jim extensions\%1\makefile.jam extensions\%1\makefile.bcc
cd extensions\%1
call configure_vc7.cmd
call configure_bcc.cmd
cd ..\..
