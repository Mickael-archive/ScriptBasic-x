/* FILE: curlintrfc.c

This file contains code the CURL interface

--GNU LGPL
This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/
#include <stdio.h>
#include <time.h>
#include <curl/curl.h>
#include "../../basext.h"

/**
=H The module CURL
=abstract
This module supports the CURL interface that eases URL access
=end


*/

typedef struct _CurlClass {
  CURL *myConnection;
  } CurlClass,*pCurlClass;

SUPPORT_MULTITHREAD

besVERSION_NEGOTIATE

  return (int)INTERFACE_VERSION;

besEND

besSUB_SHUTDOWN
  curl_global_cleanup();
besEND

besSUB_START
  pCurlClass pCurl;
  /* This macro increments the thread counter used to help to keep this library in memory 
     even those times when the process is idle and no ScriptBasic threads are running. */
  INC_THREAD_COUNTER
  /* A process level initialization of the module should take place in this function because
     the call-back support functions are not accessible when the  function DllMan or _init is
     called. On the other hand it has to initialize only once.
  */
  INITLOCK /* lock the init mutex */
  if( iFirst ){ /* if it was not initialized yet*/

    curl_global_init(CURL_GLOBAL_ALL);

    iFirst = 0;/* successful initialization was done*/
    }
  INITUNLO /* unlock the init mutex */

  besMODULEPOINTER = besALLOC(sizeof(CurlClass));
  if( besMODULEPOINTER == NULL )return 0;
  pCurl = (pCurlClass)besMODULEPOINTER;

  return COMMAND_ERROR_SUCCESS;
besEND

/**
=section
=H 

Set the session for the script.
*/
//besFUNCTION()
//  pCurlClass pCurl;

//  pCurl = besMODULEPOINTER;

//besEND

besSUB_PROCESS_START
  INIT_MULTITHREAD
  return 1;
besEND

besSUB_PROCESS_FINISH
  FINISH_MULTITHREAD
besEND

SLFST CURL_SLFST[] ={

{ "shutmodu" , shutmodu },
{ "bootmodu" , bootmodu },
{ "versmodu" , versmodu },
{ "_init" , _init },
{ "_fini" , _fini },
{ NULL , NULL }
  };
