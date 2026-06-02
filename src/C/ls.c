#include "filesys.h"
#include <stdio.h>
#include <string.h>

void ls_long(void) {
    int i, k;
    struct inode *temp_inode;
    unsigned int di_mode;
    int one;

    printf("\n%s\n", get_current_path());
    printf("Permissions       Size  Inode  Name\n");
    printf("------------------------------------\n");

    for (i = 0; i < dir.size; i++) {
        if (dir.direct[i].d_ino == DIEMPTY)
            continue;

        temp_inode = iget(dir.direct[i].d_ino);
        if (!temp_inode) {
            printf("??????????      ???    ????  %14s\n", dir.direct[i].d_name);
            continue;
        }

        di_mode = temp_inode->di_mode;

        printf("%c", (di_mode & DIDIR) ? 'd' : '-');
        
        for (k = 0; k < 9; k++) {
            one = di_mode % 2;
            di_mode = di_mode / 2;
            if (k % 3 == 0) printf("%c", one ? 'r' : '-');
            else if (k % 3 == 1) printf("%c", one ? 'w' : '-');
            else printf("%c", one ? 'x' : '-');
        }

        printf("  %6lu  %5u  %s\n",
               temp_inode->di_size,
               temp_inode->i_ino,
               dir.direct[i].d_name);

        iput(temp_inode);
    }
}
