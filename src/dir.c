#include "filesys.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * _dir: list the current directory contents.
 *
 * Original bug fixes:
 *  - Fixed "%DIRSIZs" format string typo → "%14s".
 *  - Fixed nested-loop variable reuse: the inner loop over permission
 *    bits reused 'i', corrupting the outer loop.  Changed to 'k'.
 *  - Added iput() call for temp_inode (missing in original — leaked refs).
 */
void _dir(void) {
    int i, k;
    struct inode *temp_inode;
    unsigned int di_mode;
    int one;
    unsigned int blkno;

    printf("\nCURRENT DIRECTORY ..\n");
    for (i = 0; i < dir.size; i++) {
        if (dir.direct[i].d_ino == DIEMPTY)
            continue;

        printf("%14s  ", dir.direct[i].d_name);

        temp_inode = iget(dir.direct[i].d_ino);
        if (!temp_inode) {
            printf("(unable to read inode)\n");
            continue;
        }

        di_mode = temp_inode->di_mode;
        for (k = 0; k < 9; k++) {
            one = di_mode % 2;
            di_mode = di_mode / 2;
            printf("%c", one ? 'x' : '-');
        }

        if (temp_inode->di_mode & DIFILE) {
            printf("  %lu", temp_inode->di_size);
            printf("  block chain:");
            for (k = 0; k < (int)(temp_inode->di_size / BLOCKSIZ) + 1; k++) {
                blkno = bmap(temp_inode, (unsigned int)k, 0);
                if (blkno != 0 && blkno != DISKFULL)
                    printf(" %u", blkno);
            }
            printf("\n");
        } else {
            printf("  <dir>\n");
        }

        iput(temp_inode);
    }
}

/*
 * mkdir: create a new subdirectory.
 *
 * Original bug fixes:
 *  - namei() now returns d_ino; check uses 0 consistently.
 *  - iname() now correctly copies name into the directory entry.
 *  - di_size calculation uses actual struct sizes.
 */
void mkdir(const char *name) {
    int dirpos;
    unsigned int dirid;
    struct inode *inode;
    struct direct buf[BLOCKSIZ / (DIRSIZ + 2)];
    unsigned int block;

    dirid = namei(name);
    if (dirid != 0) {
        inode = iget(dirid);
        if (inode->di_mode & DIDIR)
            printf("\n%s directory already exists!\n", name);
        else
            printf("\n%s is a file, cannot create directory with same name\n", name);
        iput(inode);
        return;
    }

    dirpos = iname(name);
    inode = ialloc();
    if (!inode) return;

    dir.direct[dirpos].d_ino = inode->i_ino;
    dir.size++;

    /* initialize the new directory with "." and ".." entries */
    memset(buf, 0, sizeof(buf));
    strcpy(buf[0].d_name, ".");
    buf[0].d_ino = inode->i_ino;
    strcpy(buf[1].d_name, "..");
    buf[1].d_ino = cur_path_inode ? cur_path_inode->i_ino : 0;

    block = balloc();
    if (block == DISKFULL) {
        printf("\nmkdir: disk full\n");
        iput(inode);
        return;
    }

    fseek(fd, DATASTART + (long)block * BLOCKSIZ, SEEK_SET);
    fwrite(buf, 1, BLOCKSIZ, fd);

    inode->di_size   = 2 * (DIRSIZ + 2);
    inode->di_number = 1;
    inode->di_mode   = user[user_id].u_default_mode | DIDIR;
    inode->di_uid    = user[user_id].u_uid;
    inode->di_gid    = user[user_id].u_gid;
    inode->di_addr[0] = block;

    sync_dir();
    iput(inode);
}

/*
 * chdir: change the current working directory.
 *
 * Original bug fixes:
 *  - Fixed packing loop: the original swapped dir.direct entries
 *    in a broken way (used memcpy with overlapping regions when
 *    the same entry was selected).  Rewritten to compact in place.
 *  - Fixed write-back loop index bug (original used index j
 *    uninitialized).
 *  - namei() now returns d_ino consistently.
 */
void sync_dir(void) {
    unsigned int block;
    int i, j, k;

    if (!cur_path_inode) return;

    /* compact the current directory: remove empty entries */
    j = 0;
    for (i = 0; i < dir.size; i++) {
        if (dir.direct[i].d_ino != 0) {
            if (i != j)
                memcpy(&dir.direct[j], &dir.direct[i], sizeof(struct direct));
            j++;
        }
    }
    dir.size = j;

    /* write back current directory blocks to disk: free all first */
    itrunc(cur_path_inode);

    k = 0;
    for (i = 0; i < dir.size; i += BLOCKSIZ / (DIRSIZ + 2)) {
        block = balloc();
        if (block == DISKFULL) break;
        /* directories are small — always fit in direct blocks (k < NDADDR) */
        cur_path_inode->di_addr[k++] = block;
        fseek(fd, DATASTART + (long)block * BLOCKSIZ, SEEK_SET);
        fwrite(&dir.direct[i], 1, BLOCKSIZ, fd);
    }
    cur_path_inode->di_size = dir.size * (DIRSIZ + 2);
    /* cur_path_inode will be written to disk soon, but here we just update memory. 
       Note: we should not iput() cur_path_inode here because it's still being used. */
}

void chdir(const char *name) {
    unsigned int dirid;
    struct inode *inode;
    int i, j;

    dirid = namei(name);
    if (dirid == 0) {
        printf("\n%s does not exist\n", name);
        return;
    }

    inode = iget(dirid);
    if (!inode) return;

    if (!(inode->di_mode & DIDIR)) {
        printf("\n%s is not a directory\n", name);
        iput(inode);
        return;
    }

    if (!access(inode->i_ino, EXICUTE)) {
        printf("\naccess denied to directory %s\n", name);
        iput(inode);
        return;
    }

    sync_dir();
    iput(cur_path_inode);

    /* switch to the new directory */
    cur_path_inode = inode;

    /* read the new directory contents (via bmap for indirect support) */
    j = 0;
    for (i = 0; i < (int)(inode->di_size / BLOCKSIZ) + 1; i++) {
        unsigned int blkno = bmap(inode, (unsigned int)i, 0);
        if (blkno == 0 || blkno == DISKFULL) continue;
        fseek(fd, DATASTART + (long)blkno * BLOCKSIZ, SEEK_SET);
        fread(&dir.direct[j], 1, BLOCKSIZ, fd);
        j += BLOCKSIZ / (DIRSIZ + 2);
    }
    dir.size = j;

    /* update current path */
    const char *current = get_current_path();
    char new_path[512];
    
    if (strcmp(name, "..") == 0) {
        char *last_slash = strrchr(current, '/');
        if (last_slash && last_slash != current) {
            *last_slash = '\0';
            strcpy(new_path, current);
            *last_slash = '/';
        } else {
            strcpy(new_path, "/");
        }
    } else if (strcmp(name, ".") == 0) {
        strcpy(new_path, current);
    } else {
        if (strcmp(current, "/") == 0) {
            snprintf(new_path, sizeof(new_path), "/%s", name);
        } else {
            snprintf(new_path, sizeof(new_path), "%s/%s", current, name);
        }
    }
    set_current_path(new_path);
}
