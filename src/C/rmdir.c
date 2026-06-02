#include "filesys.h"
#include <stdio.h>
#include <string.h>

void rmdir(const char *name) {
    unsigned int dirid;
    struct inode *inode;
    struct direct buf[BLOCKSIZ / (DIRSIZ + 2)];
    int i, count;

    dirid = namei(name);
    if (dirid == 0) {
        printf("\nrmdir: %s does not exist\n", name);
        return;
    }

    inode = iget(dirid);
    if (!inode) {
        printf("\nrmdir: cannot get inode for %s\n", name);
        return;
    }

    if (!(inode->di_mode & DIDIR)) {
        printf("\nrmdir: %s is not a directory\n", name);
        iput(inode);
        return;
    }

    if (inode->i_ino == 1) {
        printf("\nrmdir: cannot remove root directory\n");
        iput(inode);
        return;
    }

    count = 0;
    for (i = 0; i < (int)(inode->di_size / BLOCKSIZ) + 1; i++) {
        unsigned int blkno = bmap(inode, (unsigned int)i, 0);
        if (blkno == 0 || blkno == DISKFULL) continue;
        fseek(fd, DATASTART + (long)blkno * BLOCKSIZ, SEEK_SET);
        fread(buf, 1, BLOCKSIZ, fd);
        for (int j = 0; j < BLOCKSIZ / (DIRSIZ + 2); j++) {
            if (buf[j].d_ino != DIEMPTY) {
                if (strcmp(buf[j].d_name, ".") != 0 && strcmp(buf[j].d_name, "..") != 0) {
                    count++;
                }
            }
        }
    }

    if (count > 0) {
        printf("\nrmdir: %s is not empty\n", name);
        iput(inode);
        return;
    }

    itrunc(inode);
    ifree(inode->i_ino);
    iput(inode);

    for (i = 0; i < dir.size; i++) {
        if (strcmp(dir.direct[i].d_name, name) == 0) {
            dir.direct[i].d_ino = DIEMPTY;
            break;
        }
    }

    printf("\nrmdir: %s removed\n", name);
}
