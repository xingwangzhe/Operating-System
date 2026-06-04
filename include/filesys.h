#ifndef FILESYS_H
#define FILESYS_H

#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ====== Debug output (define FS_DEBUG to enable) ====== */
#ifdef FS_DEBUG
#define debug_log(...) do { printf(__VA_ARGS__); fflush(stdout); } while(0)
#else
#define debug_log(...) ((void)0)
#endif

/*
 * ====== Disk layout & block numbering ======
 *
 * Block numbering scheme:  block numbers are 0-based ABSOLUTE disk block
 * indices.  Byte offset = blkno * BLOCKSIZ.
 *
 *   Block  0        — boot block (unused by filesystem)
 *   Block  1        — superblock (struct filsys)
 *   Blocks 2..33    — inode table (DINODEBLK blocks, 2 + DINODEBLK = 34 total)
 *   Blocks 34..end  — data blocks (FILEBLK blocks starting from block 34)
 *
 * DATASTART is the byte offset to block 34 (first data block).
 * DATA area block indices:  0..FILEBLK-1  (512 data blocks, block numbers
 * relative to DATASTART).  A "data block index" maps to absolute block:
 *   abs_blk = (DATASTART / BLOCKSIZ) + data_blk_index
 *
 * In orig code, di_addr[] and s_free[] store DATA BLOCK INDICES, not
 * absolute block numbers.  bread()/bwrite() take ABSOLUTE block numbers.
 *
 * Free block range (data block indices):  [3, FILEBLK-1] inclusive.
 * s_nfree = FILEBLK - 3 = total free blocks in the system.
 * s_pfree = count of cached free blocks currently in s_free[].
 *
 * Sentinel:  FILEBLK+1 (=513) marks end of the free-block chain on disk.
 *
 * Key values:
 *   BLOCKSIZ    = 512   (bytes per block)
 *   DINODEBLK   =  32   (blocks in inode table)
 *   FILEBLK     = 512   (data block count)
 *   DATASTART   = (2 + DINODEBLK) * BLOCKSIZ = 17408
 *   Superblock  = absolute block 1, byte offset BLOCKSIZ
 *   Inode table = absolute blocks 2..33
 *   Data area   = absolute blocks 34..545
 *   NICFREE     =  50   (max cached free blocks in superblock)
 */

/* ====== Constants from original FILESYS.H ====== */
#define BLOCKSIZ    512
#define SYSOPENFILE 40
#define DIRNUM      128
#define DIRSIZ      14
#define PWDSIZ      12
#define PWDNUM      32
#define NOFILE      20
#define NADDR       10
#define NHINO       128
#define USERNUM     10
#define DINODESIZ   32
#define DINODEBLK   32
#define FILEBLK     512
#define NICFREE     50
#define NICINOD     50

/*
 * Indirect block addressing (di_addr layout).
 *
 *   di_addr[0..NDADDR-1]  — direct blocks
 *   di_addr[NDADDR]       — single indirect
 *   di_addr[NDADDR+1]     — double indirect
 *   di_addr[NDADDR+2]     — triple indirect
 */
#define NDADDR      7                     /* direct address slots (0..6)       */
#define NIADDR      3                     /* indirect address slots (7..9)     */
#define NINDIRECT   (BLOCKSIZ / sizeof(unsigned int))  /* 128 ptrs/indir blk */
#define DINODESTART (2 * BLOCKSIZ)
#define DATASTART   ((2 + DINODEBLK) * BLOCKSIZ)
#define DIEMPTY     00000
#define DIFILE      01000
#define DIDIR       02000
#define UDIREAD     00400
#define UDIWRITE    00200
#define UDIEXICUTE  00100
#define GDIREAD     00040
#define GDIWRITE    00020
#define GDIEXICUTE  00010
#define ODIREAD     00004
#define ODIWRITE    00002
#define ODIEXICUTE  00001
#define READ        1
#define WRITE       2
#define EXICUTE     3
#define DEFAULTMODE 00755
#define IUPDATE     00002
#define SUPDATE     00001
#define FREAD       00001
#define FWRITE      00002
#define FAPPEND     00004
#define DISKFULL    65535
#define SEEK_SET    0

/* ====== Core data structures ====== */

struct inode {
    struct inode *i_forw;
    struct inode *i_back;
    char          i_flag;
    unsigned int  i_ino;
    unsigned int  i_count;
    unsigned short di_number;
    unsigned short di_mode;
    unsigned short di_uid;
    unsigned short di_gid;
    unsigned long  di_size;        /* was unsigned short — widened for indir blk */
    unsigned int   di_addr[NADDR];
};

struct dinode {
    unsigned short di_number;
    unsigned short di_mode;
    unsigned short di_uid;
    unsigned short di_gid;
    unsigned long  di_size;
    unsigned int   di_addr[NADDR];
};

struct direct {
    char         d_name[DIRSIZ];
    unsigned int d_ino;
};

struct filsys {
    unsigned short s_isize;
    unsigned long  s_fsize;
    unsigned int   s_nfree;
    unsigned short s_pfree;
    unsigned int   s_free[NICFREE];
    unsigned int   s_ninode;
    unsigned short s_pinode;
    unsigned int   s_inode[NICINOD];
    unsigned int   s_rinode;
    char           s_fmod;
};

struct pwd {
    unsigned short p_uid;
    unsigned short p_gid;
    char           p_name[PWDSIZ];
    char           password[PWDSIZ];
};

struct dir {
    struct direct direct[DIRNUM];
    int          size;
};

struct hinode {
    struct inode *i_forw;
};

struct file {
    char          f_flag;
    unsigned int  f_count;
    struct inode *f_inode;
    unsigned long f_off;
};

struct user {
    unsigned short u_default_mode;
    unsigned short u_uid;
    unsigned short u_gid;
    unsigned short u_ofile[NOFILE];
};

/* ====== Global variables ====== */
extern struct hinode  hinode[NHINO];
extern struct dir     dir;
extern struct file    sys_ofile[SYSOPENFILE];
extern struct filsys  filsys;
extern struct pwd     pwd[PWDNUM];
extern struct user    user[USERNUM];
extern FILE          *fd;
extern struct inode  *cur_path_inode;
extern int            user_id;
extern int            logged_in;

/* ====== A-layer (block/inode alloc) ====== */
unsigned int   balloc(void);
void           bfree(unsigned int blkno);
void           bread(unsigned int blkno, char *buf);
void           bwrite(unsigned int blkno, const char *buf);

struct inode  *iget(unsigned int ino);
void           iput(struct inode *ip);

struct inode  *ialloc(void);
void           ifree(unsigned int ino);

/* ====== Indirect block support ====== */
unsigned int   bmap(struct inode *ip, unsigned int bn, int create);
void           itrunc(struct inode *ip);

/* ====== High-level file/dir ops ====== */
unsigned int   namei(const char *name);
unsigned short iname(const char *name);
unsigned int   access(unsigned int ino, unsigned int mode);
void           _dir(void);
void           mkdir(const char *name);
void           sync_dir(void);
void           chdir(const char *name);
unsigned short aopen(const char *name, unsigned int mode);
int            creat(const char *name);
unsigned int   fs_read(int fildes, char *buf, unsigned int size);
unsigned int   fs_write(int fildes, const char *buf, unsigned int size);
int            login(const char *name, const char *pass);
void           logout(void);
void           install(const char *image_path);
void           format(const char *image_path);
void           close(int fd);
void           halt(void);
void           delete(const char *name);
void           init_root_dir(void);
int            run_all_tests(void);
int            file_main(void);

/* ====== C-layer (user utilities) ====== */
void           my_pwd(void);
void           rmdir(const char *name);
void           cat(const char *name);
void           clear(void);
void           cp(const char *src, const char *dst);
void           ln(const char *src, const char *dst);
void           mv(const char *src, const char *dst);
void           ls_long(void);
void           find(const char *pattern);
void           grep(const char *pattern, const char *filename);
void           set_current_path(const char *path);
const char    *get_current_path(void);

#ifdef __cplusplus
}
#endif

#endif /* FILESYS_H */
