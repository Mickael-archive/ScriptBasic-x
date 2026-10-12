/*
FILE: tools.c

ScriptBasic module with handy functions.

*/

/**
=H Tools module

This module contains handy functions. These are general, system-independant functions
that can be used to write effective programs, but are more complex or special
purpose to include them into the core language.

The functions include file handling, string handling, array to string conversion and
many more. See below:
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

static long sersize(VARIABLE vVAR, pSupportTable pSt){
  long i;
  unsigned long size;

  besDEREFERENCE(vVAR);
  if( vVAR == NULL )return 1;
  switch( TYPE(vVAR) ){
    case VTYPE_ARRAY :
      size = 2*sizeof(long)+1;
      for( i = vVAR->ArrayLowLimit ; i <= vVAR->ArrayHighLimit ; i++ ){
        size += sersize(vVAR->Value.aValue[i-vVAR->ArrayLowLimit],pSt);
        }
      return size;
    case VTYPE_LONG: return sizeof(long)+1;
    case VTYPE_DOUBLE: return sizeof(double)+1;
    case VTYPE_STRING: return sizeof(long)+STRLEN(vVAR)+1;
    default: return 0;
    }
  }

static long serconv(VARIABLE vVAR, unsigned char *buf){
  long i;
  unsigned long size;
  unsigned long chunk;

  size = 0;
  if( vVAR == NULL ){
    *buf++ = VTYPE_UNDEF;
    return 1;
    }
  switch( TYPE(vVAR) ){
    case VTYPE_ARRAY :
      *buf++ = VTYPE_ARRAY;
      memcpy(buf,&(vVAR->ArrayLowLimit),sizeof(long));
      buf += sizeof(long);
      memcpy(buf,&(vVAR->ArrayHighLimit),sizeof(long));
      buf += sizeof(long);
      size = 2*sizeof(long)+1;
      for( i = vVAR->ArrayLowLimit ; i <= vVAR->ArrayHighLimit ; i++ ){
        chunk = serconv(vVAR->Value.aValue[i-vVAR->ArrayLowLimit],buf);
        buf += chunk;
        size += chunk;
        }
      return size;
    case VTYPE_LONG:
      *buf++ = VTYPE_LONG;
      memcpy(buf,&(LONGVALUE(vVAR)),sizeof(long));
      return sizeof(long)+1;
    case VTYPE_DOUBLE:
      *buf++ = VTYPE_DOUBLE;
      memcpy(buf,&(DOUBLEVALUE(vVAR)),sizeof(double));
      return sizeof(double)+1;
    case VTYPE_STRING:
      *buf++ = VTYPE_STRING;
      memcpy(buf,&(STRLEN(vVAR)),sizeof(long));
      buf += sizeof(long);
      memcpy(buf,STRINGVALUE(vVAR),STRLEN(vVAR));
      return sizeof(long)+STRLEN(vVAR)+1;
    default: return 0;
    }
  }

static VARIABLE unserconv(pExecuteObject pEo, unsigned char **buf,int *piError){
  long i;
  pSupportTable pSt;
  VARIABLE vRET;
  long ArrayLowLimit,ArrayHighLimit;
  unsigned long StringLen;

  pSt = pEo->pST;
  *piError = COMMAND_ERROR_SUCCESS;
  switch( **buf ){
    case VTYPE_ARRAY :
      (*buf)++;
      memcpy(&ArrayLowLimit,*buf,sizeof(long));
      (*buf) += sizeof(long);
      memcpy(&ArrayHighLimit,*buf,sizeof(long));
      (*buf) += sizeof(long);
      vRET = besNEWARRAY(ArrayLowLimit,ArrayHighLimit);
      if( vRET == NULL ){ *piError = COMMAND_ERROR_MEMORY_LOW; return NULL;}
      for( i = ArrayLowLimit ; i <= ArrayHighLimit ; i++ ){
        vRET->Value.aValue[i-ArrayLowLimit] = unserconv(pEo,buf,piError);
        }
      return vRET;
    case VTYPE_LONG:
      (*buf)++;
      vRET = besNEWLONG;
      if( vRET == NULL ){ *piError = COMMAND_ERROR_MEMORY_LOW; return NULL;}
      memcpy(&(LONGVALUE(vRET)),*buf,sizeof(long));
      (*buf) += sizeof(long);
      return vRET;
    case VTYPE_DOUBLE:
      (*buf)++;
      vRET = besNEWDOUBLE;
      if( vRET == NULL ){ *piError = COMMAND_ERROR_MEMORY_LOW; return NULL;}
      memcpy(&(DOUBLEVALUE(vRET)),*buf,sizeof(double));
      (*buf) += sizeof(double);
      return vRET;
    case VTYPE_STRING:
      (*buf)++;
      memcpy(&StringLen,*buf,sizeof(long));
      (*buf) += sizeof(long);
      vRET = besNEWSTRING(StringLen);
      if( vRET == NULL ){ *piError = COMMAND_ERROR_MEMORY_LOW; return NULL;}
      memcpy(STRINGVALUE(vRET),*buf,StringLen);
      (*buf) += StringLen;
      return vRET;
    case VTYPE_UNDEF:
      (*buf)++;
      return NULL;
    default: *piError = COMMAND_ERROR_ARGUMENT_RANGE;
             return NULL;
    }
  }

/**
=section ArrayToString
=H s = t::ArrayToString(Array)

Call this function to convert an array to a binary string. This string
can be saved to a file or stored in memory and the function R<StringToArray>
can convert it back to array. Note that arrays and associative arrays
are not different in ScriptBasic.

Note that the include file T<t.bas> also defines the T<Array2String> name to the same
function.

Also note that though the name of the function is T<ArrayToString> you can convert to
binary string using this function. However this is useful when the argumentum is an
array.
*/
besCOMMAND(serialize)
  NODE nItem;
  VARIABLE vARR;
  unsigned long size;

  besASSERT_COMMAND

  /* this is a function and not a command, therefore we do not have our own mortal list */
  USE_CALLER_MORTALS;

  /* evaluate the parameter */
  nItem = besPARAMETERLIST;
  if( ! nItem ){
    RESULT = NULL;
    RETURN;
    }
  vARR = _besEVALUATEEXPRESSION_A(CAR(nItem));
  ASSERTOKE;
  size = sersize(vARR,pSt);
  RESULT = besNEWMORTALSTRING(size);
  if( RESULT == NULL )ERROR(COMMAND_ERROR_MEMORY_LOW);
  serconv(vARR,STRINGVALUE(RESULT));
besEND_COMMAND

/**
=section StringToArray
=H t::StringToArray array,string

Call this subroutine to convert a string that was created as the result of
calling the function R<ArrayToString>. Note that the include file T<t.bas>
also defines the T<String2Array> function name to get to the same function.

If the string is invalid the function raises T<COMMAND_ERROR_ARGUMENT_RANGE>
error that can be captured using the command T<ON ERROR GOTO>.

It is not an error if there are excess characters in the string after the
string that was originally created by the function T<ArrayToSTring>
*/
besFUNCTION(unserialize)
  VARIABLE Argument;
  LEFTVALUE Lval;
  int iError;
  unsigned long __refcount_;
  unsigned char *s;

  Argument = besARGUMENT(1);
  besLEFTVALUE(Argument,Lval);
  Argument = besARGUMENT(2);
  besDEREFERENCE(Argument);
  Argument = besCONVERT2STRING(Argument);
  s = STRINGVALUE(Argument);
  if( Lval ){
    besRELEASE(*Lval);
    *Lval = unserconv(pSt->pEo,&s,&iError);
    }
  return iError;
besEND

/**
=section savestring
=H t::SaveString file,string

Save a string to a file. This subroutine opens the file, creates it if it did not exist
and prints the binary string to the file and closes the file.
See also R<loadstring>
*/
besFUNCTION(savestring)
  unsigned char *pszString;
  char *pszFileName;
  unsigned long cbString;
  FILE *fp;
  VARIABLE Argumentum;

  if( besARGNR < 2 )return COMMAND_ERROR_MANDARG;

  Argumentum = besARGUMENT(1);
  besDEREFERENCE(Argumentum);
  Argumentum = besCONVERT2STRING(Argumentum);
  besCONVERT2ZCHAR(Argumentum,pszFileName);

  Argumentum = besARGUMENT(2);
  besDEREFERENCE(Argumentum);
  Argumentum = besCONVERT2STRING(Argumentum);

  pszString = STRINGVALUE(Argumentum);
  cbString = STRLEN(Argumentum);
  fp = besHOOK_FOPEN(pszFileName,"wb");
  if( fp == NULL ){
    besFREE(pszFileName);
    return COMMAND_ERROR_FILE_CANNOT_BE_OPENED;
    }
  besHOOK_FWRITE(pszString,1,cbString,fp);
  besHOOK_FCLOSE(fp);

  besFREE(pszFileName);

besEND

/**
=section loadstring
=H s = t::LoadString(file)

This function opens a file, reads its content into a new string variable and returns it.
See also R<savestring>

*/
besFUNCTION(loadstring)
  char *pszFileName;
  unsigned long cbString;
  FILE *fp;
  VARIABLE Argumentum;

  if( besARGNR < 1 )return COMMAND_ERROR_MANDARG;

  Argumentum = besARGUMENT(1);
  besDEREFERENCE(Argumentum);
  Argumentum = besCONVERT2STRING(Argumentum);
  besCONVERT2ZCHAR(Argumentum,pszFileName);

  cbString = besHOOK_SIZE(pszFileName);
  besRETURNVALUE = besNEWMORTALSTRING(cbString);
  if( besRETURNVALUE == NULL ){
    besFREE(pszFileName);
    return COMMAND_ERROR_MEMORY_LOW;
    }

  fp = besHOOK_FOPEN(pszFileName,"rb");
  if( fp == NULL ){
    besFREE(pszFileName);
    return COMMAND_ERROR_FILE_CANNOT_BE_OPENED;
    }
  besHOOK_FREAD(STRINGVALUE(besRETURNVALUE),1,cbString,fp);
  besHOOK_FCLOSE(fp);

  besFREE(pszFileName);

besEND

/**
=section MD5
=H Calculate MD5
Calculate the MD5 value of a string

=verbatim
  q = t::MD5("string")
=noverbatim

Note that the return value is a 16 byte binary strig thus do not try to print it
without first converting to some printable string.

*/
besFUNCTION(md5fun)
  VARIABLE Argument;
  MD5_CTX Md5Context;

  besASSERT_FUNCTION

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

/**
=section ArrayToStringMD5
=H s = t::ArrayToStringMD5(Array)

This function is the same as R<ArrayToString> with the extra feature that this function
appends the MD5 fingerprint of the created string to the result, generating 16 characters
longer result. This is a bit slower than R<ArrayToString> and creates longer string, but also
ensures integrity and can eliminate file reading and write errors.

See also R<StringToArrayMD5>.

Note that because R<StringToArray> does not care excess characters at the end of the string
you can use R<StringToArray> to convert a string back to array that was created using
T<ArrayToStringMD5>.
*/
besCOMMAND(md5serialize)
  NODE nItem;
  VARIABLE vARR;
  unsigned long size;
  MD5_CTX Md5Context;

  besASSERT_COMMAND

  /* this is a function and not a command, therefore we do not have our own mortal list */
  USE_CALLER_MORTALS;

  /* evaluate the parameter */
  nItem = besPARAMETERLIST;
  if( ! nItem ){
    RESULT = NULL;
    RETURN;
    }
  vARR = _besEVALUATEEXPRESSION_A(CAR(nItem));
  ASSERTOKE;
  size = sersize(vARR,pSt);
  RESULT = besNEWMORTALSTRING(size + 16);
  if( RESULT == NULL )ERROR(COMMAND_ERROR_MEMORY_LOW);
  serconv(vARR,STRINGVALUE(RESULT));
  besMD5INIT( &Md5Context);
  besMD5UPDATE(&Md5Context,STRINGVALUE(RESULT),size);

  besMD5FINAL(STRINGVALUE(RESULT)+size,&Md5Context);

besEND_COMMAND

/**
=section StringToArrayMD5
=H t::StringToArrayMD5 array,string

This function is the same as R<StringToArray> with the extra functionality that this
function is the counterpart of the function R<ArrayToStringMD5> and this function
does check MD5 integrity of the strig before starting to convert back.
*/
besFUNCTION(md5unserialize)
  VARIABLE Argument;
  LEFTVALUE Lval;
  int iError;
  unsigned long __refcount_;
  unsigned char *s;
  MD5_CTX Md5Context;
  unsigned char MD5[16];

  Argument = besARGUMENT(1);
  besLEFTVALUE(Argument,Lval);
  Argument = besARGUMENT(2);
  besDEREFERENCE(Argument);
  Argument = besCONVERT2STRING(Argument);
  s = STRINGVALUE(Argument);
  besMD5INIT( &Md5Context);
  besMD5UPDATE(&Md5Context,s,STRLEN(Argument)-16);
  besMD5FINAL(MD5,&Md5Context);
  if( memcmp(s+STRLEN(Argument)-16,MD5,16) )return COMMAND_ERROR_ARGUMENT_RANGE;

  if( Lval ){
    besRELEASE(*Lval);
    *Lval = unserconv(pSt->pEo,&s,&iError);
    }
  return iError;
besEND
