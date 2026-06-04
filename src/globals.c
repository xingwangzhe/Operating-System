#include "filesys.h"

/* disk image file handle */
FILE *fd = NULL;

/* superblock (in-memory copy) */
struct filsys filsys;

/* inode hash table */
struct hinode hinode[NHINO];

/* system open file table */
struct file sys_ofile[SYSOPENFILE];

/* password table */
struct pwd pwd[PWDNUM];

/* user table */
struct user user[USERNUM];

/* current directory */
struct dir dir;

/* current path inode */
struct inode *cur_path_inode = NULL;

/* current user id */
int user_id = 0;

/* login state: 0 = not logged in, 1 = logged in */
int logged_in = 0;
