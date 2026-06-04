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
        
        /* 从高位到低位读：位8=属主r, 7=属主w, 6=属主x, 5=组r, 4=组w, 3=组x, 2=其他r, 1=其他w, 0=其他x */
        for (k = 8; k >= 0; k--) {
            one = (di_mode >> (unsigned int)k) & 1;
            if (k % 3 == 2) printf("%c", one ? 'r' : '-');   /* 读位 */
            else if (k % 3 == 1) printf("%c", one ? 'w' : '-'); /* 写位 */
            else printf("%c", one ? 'x' : '-');                /* 执行位 */
        }

        printf("  %6lu  %5u  %s\n",
               temp_inode->di_size,
               temp_inode->i_ino,
               dir.direct[i].d_name);

        iput(temp_inode);
    }
}
