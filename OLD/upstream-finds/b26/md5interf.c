/* FILE: md5interf.c

This file is a ScriptBasic interface file to the library md5

*/
#include <stdio.h>
#include "../../basext.h"

besVERSION_NEGOTIATE

  return (int)INTERFACE_VERSION;

besEND

besSUB_START

besEND

besSUB_FINISH

besEND

/**
=H The module MD5
=abstract
Calculate the MD5 value of a string
=end

This module delivers a single function T<MD5> that returns the
MD5 digest of its argument.

=verbatim
  q = MD5("string")
=noverbatim

Note that the return value is a 16 byte binary strig thus do not try to print it
without first converting to some printable string.

*/
besFUNCTION(md5fun)
  VARIABLE Argument;
  MD5_CTX Md5Context;


  /* md5() or md5(undef) is undef */
  besRETURNVALUE = NULL;
  if( besARGNR < 1 )return COMMAND_ERROR_SUCCESS;
  Argument = besARGUMENT(1);
  if( Argument == NULL )return COMMAND_ERROR_SUCCESS;
  besDEREFERENCE(Argument);

  besALLOC_RETURN_STRING(16);

  Argument = besCONVERT2STRING(Argument);

  besMD5INIT( &Md5Context);
  besMD5UPDATE(&Md5Context,STRINGVALUE(Argument),STRLEN(Argument));

  besMD5FINAL(STRINGVALUE(besRETURNVALUE),&Md5Context);

besEND
