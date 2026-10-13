/*
filesys.h
*/
#ifndef __FILESYS_H__
#define __FILESYS_H__ 1
#ifdef  __cplusplus
extern "C" {
#endif

#ifdef WIN32
#include <windows.h>
#include <winbase.h>
#include <io.h>
#include <direct.h>
#include <lm.h>
#include <lmaccess.h>
#else
#include <sys/file.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#ifndef TYPE_SOCKET
typedef int SOCKET;
#endif
#endif

#ifndef LOCK_SH
#define LOCK_SH 1
#endif

#ifndef LOCK_EX
#define LOCK_EX 2
#endif

#ifndef LOCK_NB
#define LOCK_NB 4
#endif

#ifndef LOCK_UN
#define LOCK_UN 8
#endif


/*FUNDEF*/

FILE *file_fopen(
  char *pszFileName,
  char *pszOpenMode);
/*FEDNUF*/
/*FUNDEF*/

void file_fclose(FILE *fp);
/*FEDNUF*/
/*FUNDEF*/

long file_size(char *pszFileName);
/*FEDNUF*/
/*FUNDEF*/

long file_time_accessed(char *pszFileName);
/*FEDNUF*/
/*FUNDEF*/

long file_time_modified(char *pszFileName);
/*FEDNUF*/
/*FUNDEF*/

long file_time_created(char *pszFileName);
/*FEDNUF*/
/*FUNDEF*/

int file_isdir(char *pszFileName);
/*FEDNUF*/
/*FUNDEF*/

int file_isreg(char *pszFileName);
/*FEDNUF*/
/*FUNDEF*/

int file_exists(char *pszFileName);
/*FEDNUF*/
/*FUNDEF*/

int file_truncate(FILE *fp,
                  long lNewFileSize);
/*FEDNUF*/
/*FUNDEF*/

int file_fgetc(FILE *fp);
/*FEDNUF*/
/*FUNDEF*/

int file_ferror(FILE *fp);
/*FEDNUF*/
/*FUNDEF*/

int file_fread(char *buf,
               int size,
               int count,
               FILE *fp);
/*FEDNUF*/
/*FUNDEF*/

int file_fwrite(char *buf,
               int size,
               int count,
               FILE *fp);
/*FEDNUF*/
/*FUNDEF*/

int file_fputc(int c, FILE *fp);
/*FEDNUF*/
/*FUNDEF*/

void file_setmode(FILE *fp,
                  int mode);
/*FEDNUF*/
/*FUNDEF*/

void file_binmode(FILE *fp);
/*FEDNUF*/
/*FUNDEF*/

void file_textmode(FILE *fp);
/*FEDNUF*/
/*FUNDEF*/

int file_flock(FILE *fp,
               int iLockType);
/*FEDNUF*/
/*FUNDEF*/

int file_lock(FILE *fp,
              int iLockType,
              long lStart,
              long lLength);
/*FEDNUF*/
/*FUNDEF*/

int file_feof(FILE *fp);
/*FEDNUF*/
/*FUNDEF*/

int file_mkdir(char *pszDirectoryName);
/*FEDNUF*/
/*FUNDEF*/

int file_rmdir(char *pszDirectoryName);
/*FEDNUF*/
/*FUNDEF*/

int file_remove(char *pszFileName);
/*FEDNUF*/
/*FUNDEF*/

int file_deltree(char *pszDirectoryName);
/*FEDNUF*/
/*FUNDEF*/

int file_MakeDirectory(char *pszDirectoryName);
/*FEDNUF*/
#ifdef WIN32
struct dirent {
  char d_name[MAX_PATH];
  };
typedef struct _OpenDir {
  HANDLE hFindFile;
  WIN32_FIND_DATA FindFileData;
  struct dirent CurrentEntry;
  } DIR,tDIR;
#else
typedef unsigned char tDIR;
#endif
/*FUNDEF*/

DIR *file_opendir(char *pszDirectoryName,
                  tDIR *pDirectory);
/*FEDNUF*/
/*FUNDEF*/

struct dirent *file_readdir(DIR *pDirectory);
/*FEDNUF*/
/*FUNDEF*/

void file_closedir(DIR *pDirectory);
/*FEDNUF*/
/*FUNDEF*/

void sys_sleep(long lSeconds);
/*FEDNUF*/
/*FUNDEF*/

int file_curdir(char *Buffer,
                unsigned long cbBuffer);
/*FEDNUF*/
/*FUNDEF*/

int file_chdir(char *Buffer);
/*FEDNUF*/
/*FUNDEF*/

int file_chown(char *pszFile,
               char *pszOwner);
/*FEDNUF*/
/*FUNDEF*/

int file_getowner(char *pszFileName,
                  char *pszOwnerBuffer,
                  long cbOwnerBuffer);
/*FEDNUF*/
/*FUNDEF*/

int file_SetCreateTime(char *pszFile,
                       long lTime);
/*FEDNUF*/
/*FUNDEF*/

int file_SetModifyTime(char *pszFile,
                       long lTime);
/*FEDNUF*/
/*FUNDEF*/

int file_SetAccessTime(char *pszFile,
                       long lTime);
/*FEDNUF*/
/*FUNDEF*/

int file_gethostname(char *pszBuffer,
                     long cbBuffer);
/*FEDNUF*/
/*FUNDEF*/

int file_gethost(char *pszBuffer,
                 struct hostent *pHost);
/*FEDNUF*/
/*FUNDEF*/

int file_tcpconnect(SOCKET *sClient,
                    char *pszRemoteSocket);
/*FEDNUF*/
/*FUNDEF*/

int file_tcpsend(SOCKET sClient,
                 char *pszBuffer,
                 long cbBuffer,
                 int iFlags);
/*FEDNUF*/
/*FUNDEF*/

int file_tcprecv(SOCKET sClient,
                 char *pszBuffer,
                 long cbBuffer,
                 int iFlags);
/*FEDNUF*/
/*FUNDEF*/

int file_tcpclose(SOCKET sClient);
/*FEDNUF*/
/*FUNDEF*/

int file_killproc(long pid);
/*FEDNUF*/
/*FUNDEF*/

char *file_fcrypt(char *buf, char *salt, char *buff);
/*FEDNUF*/
/*FUNDEF*/

long file_CreateProcess(char *pszCommandLine);
/*FEDNUF*/
#define FILESYSE_SUCCESS    0
#define FILESYSE_NOTSTARTED 1
#define FILESYSE_TIMEOUT    2
#define FILESYSE_NOCODE     3
/*FUNDEF*/

int file_CreateProcessEx(char *pszCommandLine,
                          long lTimeOut,
                          unsigned long *plPid,
                          unsigned long *plExitCode);
/*FEDNUF*/
/*FUNDEF*/

int file_waitpid(long pid,
                 unsigned long *plExitCode);
/*FEDNUF*/
#ifdef __cplusplus
}
#endif
#endif
