
#include "trs-fs.h"
#include "smb.h"
#include "storage.h"
#include "io.h"

#include <string.h>
#include <assert.h>

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "smb2.h"
#include "smb2/libsmb2.h"
#include "smb2/libsmb2-raw.h"

static char* smb_url = NULL;

static struct smb2_context *smb2 = NULL;

const char* init_smb()
{
  static char* smb_user = NULL;
  static char* smb_passwd = NULL;
  struct smb2_url *url;

  if (smb2 != NULL) {
    smb2_destroy_context(smb2);
    smb2 = NULL;
  }

  if (smb_url != NULL) {
    free(smb_url);
    smb_url = NULL;
  }

  if (smb_user != NULL) {
    free(smb_user);
    smb_user = NULL;
  }

  if (smb_passwd != NULL) {
    free(smb_passwd);
    smb_passwd = NULL;
  }  
 
  smb2 = smb2_init_context();
  if (smb2 == NULL) {
    return "Failed to initialize SMB";
  }

  if (!storage_has_key(SMB_KEY_URL) || !storage_has_key(SMB_KEY_USER) || !storage_has_key(SMB_KEY_PASSWD)) {
    return "Missing SMB share configuration";
  }
  
  size_t len;
  storage_get_str(SMB_KEY_URL, NULL, &len);
  smb_url = (char*) malloc(len);
  storage_get_str(SMB_KEY_URL, smb_url, &len);

  storage_get_str(SMB_KEY_USER, NULL, &len);
  smb_user = (char*) malloc(len);
  storage_get_str(SMB_KEY_USER, smb_user, &len);

  storage_get_str(SMB_KEY_PASSWD, NULL, &len);
  smb_passwd = (char*) malloc(len);
  storage_get_str(SMB_KEY_PASSWD, smb_passwd, &len);

  url = smb2_parse_url(smb2, smb_url);
  if (url == NULL) {
    return smb2_get_error(smb2);
  }

  smb2_set_security_mode(smb2, SMB2_NEGOTIATE_SIGNING_ENABLED);
  smb2_set_user(smb2, smb_user);
  smb2_set_password(smb2, smb_passwd);

  if (smb2_connect_share(smb2, url->server, url->share, url->user) != 0) {
    return smb2_get_error(smb2);
  }

  return NULL;
}


TRS_FS_SMB::TRS_FS_SMB() {
  err_msg = init_smb();
}
  
void TRS_FS_SMB::f_log(const char* msg) {
  printf("%s\n", msg);
}
  
FRESULT TRS_FS_SMB::f_open (
                            FIL* fp,           /* [OUT] Pointer to the file object structure */
                            const TCHAR* path, /* [IN] File name */
                            BYTE mode          /* [IN] Mode flags */
                            ) {
  struct smb2_url *url;
  char* smb_path;
  int m = 0;
  
  switch(mode) {
  case FA_READ:
    m = O_RDONLY;
    break;
  case FA_READ | FA_WRITE:
    m = O_RDWR;
    break;
  case FA_CREATE_ALWAYS | FA_WRITE:
    m = O_CREAT | O_WRONLY | O_TRUNC;
    break;
  case FA_CREATE_ALWAYS | FA_WRITE | FA_READ:
    m = O_CREAT | O_RDWR;
    break;
  case FA_OPEN_APPEND | FA_WRITE:
    m = O_WRONLY | O_APPEND;
    break;
  case FA_OPEN_APPEND | FA_WRITE | FA_READ:
    m = O_RDWR | O_APPEND;
    break;
  case  FA_CREATE_NEW | FA_WRITE:
    m = O_WRONLY | O_EXCL;
    break;
  case FA_CREATE_NEW | FA_WRITE | FA_READ:
    m = O_RDWR | O_EXCL;
    break;
  default:
    assert(0);
  }

  asprintf(&smb_path, "%s/%s", smb_url, path);
  url = smb2_parse_url(smb2, smb_path);
  free(smb_path);
  if (url == NULL) {
    return FR_NO_FILE;
  }

  fp->f = (struct smb2fh*) smb2_open(smb2, url->path, m);
  smb2_destroy_url(url);
  return (fp->f == NULL) ? FR_NO_FILE : FR_OK;
}

FRESULT TRS_FS_SMB::f_opendir (
                               DIR_* dp,           /* [OUT] Pointer to the directory object structure */
                               const TCHAR* path  /* [IN] Directory name */
                               ) {
  if ((strcmp(path, ".") == 0) || (strcmp(path, "/") == 0)) {
    path = "";
  }
  dp->dir = (struct smb2dir*) smb2_opendir(smb2, path);
  return (dp->dir != NULL) ? FR_OK : FR_DISK_ERR;
}

FRESULT TRS_FS_SMB::f_write (
                             FIL* fp,          /* [IN] Pointer to the file object structure */
                             const void* buff, /* [IN] Pointer to the data to be written */
                             UINT btw,         /* [IN] Number of bytes to write */
                             UINT* bw          /* [OUT] Pointer to the variable to return number of bytes written */
                             ) {
  int _bw = smb2_write(smb2, (struct smb2fh*) fp->f, (uint8_t*) buff, btw);
  *bw = _bw;
  return (_bw >= 0) ? FR_OK : FR_DISK_ERR;
}

FRESULT TRS_FS_SMB::f_read (
                            FIL* fp,     /* [IN] File object */
                            void* buff,  /* [OUT] Buffer to store read data */
                            UINT btr,    /* [IN] Number of bytes to read */
                            UINT* br     /* [OUT] Number of bytes read */
                            ) {
  int _br = smb2_read(smb2, (struct smb2fh*) fp->f, (uint8_t*) buff, btr);
  *br = _br;
  return (_br >= 0) ? FR_OK : FR_DISK_ERR;
}

FRESULT TRS_FS_SMB::f_readdir (
                               DIR_* dp,      /* [IN] Directory object */
                               FILINFO* fno  /* [OUT] File information structure */
                                  ) {
  while (1) {
    struct smb2dirent* entry = smb2_readdir(smb2, (struct smb2dir*) dp->dir);
    if (entry == NULL) {
      smb2_closedir(smb2, (struct smb2dir*) dp->dir);
      fno->fname[0] = '\0';
      break;
    }
    if (strcmp(entry->name, ".") == 0 || strcmp(entry->name, "..") == 0) {
      continue;
    }
    if (strlen(entry->name) > 12) {
      continue;
    }
    strcpy(fno->fname, entry->name);
    fno->fsize = entry->st.smb2_size;
    fno->fattrib = 1;
    break;
  }
  return FR_OK;
}

FSIZE_t TRS_FS_SMB::f_tell (
                            FIL* fp   /* [IN] File object */
                            ) {
  assert(0);
}

FRESULT TRS_FS_SMB::f_sync (
                            FIL* fp     /* [IN] File object */
                            ) {
  assert(0);
}

FRESULT TRS_FS_SMB::f_lseek (
                             FIL*    fp,  /* [IN] File object */
                             FSIZE_t ofs  /* [IN] File read/write pointer */
                             ) {
  uint64_t current_offset;
  
  smb2_lseek(smb2, (struct smb2fh*) fp->f, ofs, SEEK_SET, &current_offset);
  return FR_OK;
}
  
FRESULT TRS_FS_SMB::f_close (
                             FIL* fp     /* [IN] Pointer to the file object */
                             ) {
  smb2_close(smb2, (struct smb2fh*) fp->f);
  return FR_OK;
}

FRESULT TRS_FS_SMB::f_unlink (
                              const TCHAR* path  /* [IN] Object name */
                              ) {
  return smb2_unlink(smb2, path) ? FR_NO_FILE : FR_OK;
}

FRESULT TRS_FS_SMB::f_stat (
                            const TCHAR* path,  /* [IN] Object name */
                            FILINFO* fno        /* [OUT] FILINFO structure */
                            ) {
  struct smb2fh* fh = smb2_open(smb2, path, O_RDONLY);
  if (fh == NULL) {
    return FR_NO_FILE;
  }
  struct smb2_stat_64 st;
  if (smb2_fstat(smb2, fh, &st) != 0) {
    smb2_close(smb2, fh);
    return FR_NO_FILE;
  }
  strcpy(fno->fname, path);
  fno->fsize = st.smb2_size;
  fno->fattrib = 1; //XXX
  smb2_close(smb2, fh);
  return FR_OK;
}
