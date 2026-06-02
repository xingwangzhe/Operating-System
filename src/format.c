#include "filesys.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#define DISK_IMAGE    "build/filesystem.img"
#define DISK_BLKS     (2 + DINODEBLK + FILEBLK)

static void die(const char *msg) {
    printf("format FATAL: %s (errno=%d: %s)\n", msg, errno, strerror(errno));
    fflush(stdout);
    exit(1);
}

void format(const char *image_path) {
    int i, j;
    long disk_bytes;
    int wrote;

    if (!image_path || !image_path[0])
        image_path = "build/filesystem.img";

    debug_log("format: creating %s ...\n", image_path);

    /* initialize inode hash before any iget/iput calls */
    for (i = 0; i < NHINO; i++)
        hinode[i].i_forw = NULL;

    /* initialize system open file table */
    for (i = 0; i < SYSOPENFILE; i++) {
        sys_ofile[i].f_count = 0;
        sys_ofile[i].f_inode = NULL;
    }

    fd = fopen(image_path, "w+b");
    for (i = 0; i < USERNUM; i++) {
        user[i].u_uid          = 0;
        user[i].u_gid          = 0;
        user[i].u_default_mode = DEFAULTMODE;
        for (j = 0; j < NOFILE; j++)
            user[i].u_ofile[j] = SYSOPENFILE + 1;
    }

    fd = fopen(DISK_IMAGE, "w+b");
    if (!fd) die("cannot create disk image");

    /* pre-allocate fixed-size disk image */
    disk_bytes = (long)DISK_BLKS * BLOCKSIZ;
    debug_log("format: image size = %ld bytes (%ld blocks x %d)\n",
              disk_bytes, (long)DISK_BLKS, BLOCKSIZ);

    if (fseek(fd, disk_bytes - 1, SEEK_SET) != 0)
        die("fseek to last byte failed");
    wrote = fputc('\0', fd);
    if (wrote == EOF)
        die("fputc failed");
    if (fflush(fd) != 0)
        die("fflush failed");
    rewind(fd);

    /* --- initialize superblock in memory --- */
    filsys.s_isize  = DINODEBLK;
    filsys.s_fsize  = FILEBLK;
    filsys.s_ninode = DINODEBLK * BLOCKSIZ / DINODESIZ - 4;
    filsys.s_nfree  = FILEBLK - 3;
    filsys.s_fmod   = 0;

    debug_log("format: superblock: isize=%u fsize=%lu ninode=%u nfree=%u\n",
              (unsigned)filsys.s_isize, (unsigned long)filsys.s_fsize,
              filsys.s_ninode, filsys.s_nfree);

    /* --- inode free list: inodes 4..(4+NICINOD-1) --- */
    filsys.s_pinode = 0;
    filsys.s_rinode = NICINOD + 4;
    for (i = 0; i < NICINOD; i++)
        filsys.s_inode[i] = 4 + (unsigned int)i;

    debug_log("format: inode free list: %d entries (4..%d), rinode=%u\n",
              NICINOD, 4 + NICINOD - 1, filsys.s_rinode);

    /* --- free block list ---
     *
     * Feed free data blocks through bfree() so the free-block chain
     * is properly initialised (cache + on-disk chain blocks).
     * Free blocks: data-block indices 3 through FILEBLK-1 inclusive.
     */
    filsys.s_pfree = 0;
    filsys.s_nfree = 0;
    for (i = 3; i < FILEBLK; i++)
        bfree((unsigned int)i);

    debug_log("format: free blocks fed through bfree: s_nfree=%u, s_pfree=%u\n",
              filsys.s_nfree, filsys.s_pfree);

    /* write superblock to block 1 */
    if (fseek(fd, BLOCKSIZ, SEEK_SET) != 0)
        die("fseek to superblock failed");
    if (fwrite(&filsys, sizeof(struct filsys), 1, fd) != 1)
        die("fwrite superblock failed");
    if (fflush(fd) != 0)
        die("fflush superblock failed");

    debug_log("format: done. image=%s (%ld bytes)\n", DISK_IMAGE, disk_bytes);
}

void init_root_dir(void) {
    struct inode *root;
    unsigned int root_block;

    root = iget(1);
    if (!root) {
        printf("init_root_dir: cannot get root inode\n");
        return;
    }

    root->di_mode   = DIDIR | DEFAULTMODE;
    root->di_uid    = 0;
    root->di_gid    = 0;
    root->di_number = 1;

    root_block = balloc();
    if (root_block == DISKFULL) {
        printf("init_root_dir: disk full\n");
        iput(root);
        return;
    }

    root->di_addr[0] = root_block;
    root->di_size = 2 * (DIRSIZ + 2);

    /* write "." and ".." directory entries to disk */
    {
        struct direct dbuf[BLOCKSIZ / (DIRSIZ + 2)];
        memset(dbuf, 0, sizeof(dbuf));
        strcpy(dbuf[0].d_name, ".");
        dbuf[0].d_ino = 1;
        strcpy(dbuf[1].d_name, "..");
        dbuf[1].d_ino = 1;
        fseek(fd, DATASTART + (long)root_block * BLOCKSIZ, SEEK_SET);
        fwrite(dbuf, 1, BLOCKSIZ, fd);
    }

    /* initialize in-memory directory with "." and ".." */
    memset(&dir, 0, sizeof(dir));
    memset(dir.direct[0].d_name, 0, DIRSIZ);
    dir.direct[0].d_name[0] = '.';
    dir.direct[0].d_ino = 1;
    memset(dir.direct[1].d_name, 0, DIRSIZ);
    dir.direct[1].d_name[0] = '.';
    dir.direct[1].d_name[1] = '.';
    dir.direct[1].d_ino = 1;
    dir.size = 2;

    cur_path_inode = root;  /* transfer reference to global */
    set_current_path("/");

    debug_log("init_root_dir: root initialized, block=%u\n", root_block);
}
