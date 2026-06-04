#include "filesys.h"
#include <stdlib.h>
#include <stdio.h>

void halt(void) {
    int i, j;
    unsigned int sys_no;

    debug_log("halt: shutting down...\n");

    /* Close all open file descriptors for all users so inodes flush to disk */
    for (i = 0; i < USERNUM; i++) {
        for (j = 0; j < NOFILE; j++) {
            if (user[i].u_ofile[j] != SYSOPENFILE + 1) {
                sys_no = user[i].u_ofile[j];
                if (sys_no < SYSOPENFILE && sys_ofile[sys_no].f_inode) {
                    iput(sys_ofile[sys_no].f_inode);
                    sys_ofile[sys_no].f_count = 0;
                    sys_ofile[sys_no].f_inode = NULL;
                }
                user[i].u_ofile[j] = SYSOPENFILE + 1;
            }
        }
    }

    /* Sync memory directory buffer to disk */
    sync_dir();

    /* flush current directory inode to disk */
    if (cur_path_inode) {
        iput(cur_path_inode);
        cur_path_inode = NULL;
    }

    if (fd) {
        /* write back superblock to block 1 */
        if (fseek(fd, BLOCKSIZ, SEEK_SET) == 0) {
            if (fwrite(&filsys, sizeof(struct filsys), 1, fd) != 1)
                printf("halt WARNING: superblock write failed\n");
        }
        if (fclose(fd) != 0)
            printf("halt WARNING: fclose failed\n");
        fd = NULL;
    }

    printf("Good Bye. See You Next Time.\n");
    fflush(stdout);
    exit(0);
}
