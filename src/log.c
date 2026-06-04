#include "filesys.h"
#include <string.h>
#include <stdio.h>

/*
 * login: authenticate a user by name and password.
 * Returns 1 on success, 0 on failure.
 *
 * On success, sets up user[user_id] with uid, gid, and default mode.
 * Finds a free slot in the user table.
 *
 * Original bug fixes:
 *  - strcmp() condition was reversed: the original checked
 *    if(strcmp(passwd, pwd[i].password)) which is TRUE when
 *    passwords DON'T match.  Fixed to strcmp(...) == 0.
 *  - User gid assignment used loop index 'i' instead of 'j'
 *    (user[i].u_gid should be user[j].u_gid).
 */
int login(const char *name, const char *passwd) {
    int i, j;

    for (i = 0; i < PWDNUM; i++) {
        if (pwd[i].p_uid != 0 &&
            strcmp(name, pwd[i].p_name) == 0 &&
            strcmp(passwd, pwd[i].password) == 0) {
            /* find a free user slot */
            for (j = 0; j < USERNUM; j++)
                if (user[j].u_uid == 0) break;

            if (j == USERNUM) {
                printf("\nToo many users in the system, try again later\n");
                return 0;
            }

            user[j].u_uid          = pwd[i].p_uid;
            user[j].u_gid          = pwd[i].p_gid;
            user[j].u_default_mode = DEFAULTMODE;
            user_id = j;
            logged_in = 1;
            return 1;
        }
    }

    printf("\nIncorrect login\n");
    return 0;
}

/*
 * logout: log out the current user.
 *
 * Closes all open files and clears the user slot.
 *
 * Original bug fixes:
 *  - The original took a uid parameter but the header uses void;
 *    we use the global user_id.
 */
void logout(void) {
    int j;
    unsigned int sys_no;
    struct inode *inode;

    for (j = 0; j < NOFILE; j++) {
        if (user[user_id].u_ofile[j] != SYSOPENFILE + 1) {
            sys_no = user[user_id].u_ofile[j];
            if (sys_no < SYSOPENFILE) {
                inode = sys_ofile[sys_no].f_inode;
                iput(inode);
                if (sys_ofile[sys_no].f_count > 0)
                    sys_ofile[sys_no].f_count--;
            }
            user[user_id].u_ofile[j] = SYSOPENFILE + 1;
        }
    }

    user[user_id].u_uid = 0;
    user[user_id].u_gid = 0;
    user_id = 0;
    logged_in = 0;
}
