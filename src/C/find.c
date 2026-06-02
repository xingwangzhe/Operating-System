#include "filesys.h"
#include <stdio.h>
#include <string.h>

static void find_recursive(unsigned int dir_ino, const char *pattern, const char *path) {
    struct inode *inode;
    struct direct buf[BLOCKSIZ / (DIRSIZ + 2)];
    int i, j;
    char full_path[512];

    inode = iget(dir_ino);
    if (!inode) return;

    for (i = 0; i < (int)(inode->di_size / BLOCKSIZ) + 1; i++) {
        unsigned int blkno = bmap(inode, (unsigned int)i, 0);
        if (blkno == 0 || blkno == DISKFULL) continue;
        fseek(fd, DATASTART + (long)blkno * BLOCKSIZ, SEEK_SET);
        fread(buf, 1, BLOCKSIZ, fd);

        for (j = 0; j < BLOCKSIZ / (DIRSIZ + 2); j++) {
            if (buf[j].d_ino == DIEMPTY) continue;
            if (strcmp(buf[j].d_name, ".") == 0 || strcmp(buf[j].d_name, "..") == 0) continue;

            snprintf(full_path, sizeof(full_path), "%s/%s", path, buf[j].d_name);

            if (strstr(buf[j].d_name, pattern) != NULL) {
                printf("%s\n", full_path);
            }

            struct inode *child_inode = iget(buf[j].d_ino);
            if (child_inode && (child_inode->di_mode & DIDIR)) {
                find_recursive(buf[j].d_ino, pattern, full_path);
            }
            if (child_inode) iput(child_inode);
        }
    }

    iput(inode);
}

void find(const char *pattern) {
    if (!cur_path_inode) {
        printf("find: No filesystem mounted\n");
        return;
    }

    find_recursive(cur_path_inode->i_ino, pattern, get_current_path());
}
