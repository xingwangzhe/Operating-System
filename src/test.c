#include "filesys.h"
#include <stdio.h>
#include <string.h>

int run_all_tests(void) {
    char buf[BLOCKSIZ];
    const char *msg = "hello filesystem!";
    int ok;

    puts("=== smoke test start ===");

    /* format creates a fresh filesystem on build/filesystem.img */
    format("build/filesystem.img");

    /* --- Test bread/bwrite --- */
    memset(buf, 0, BLOCKSIZ);
    memcpy(buf, msg, strlen(msg) + 1);
    bwrite(35, buf);

    memset(buf, 0, BLOCKSIZ);
    bread(35, buf);
    ok = (strcmp(buf, msg) == 0);
    printf("bread/bwrite test: %s\n", ok ? "PASS" : "FAIL");

    /* --- Test balloc/bfree --- */
    {
        unsigned int b1, b2, b3;
        b1 = balloc();
        ok = (b1 != 0 && b1 != DISKFULL);
        printf("balloc test #1: %s (block=%u)\n", ok ? "PASS" : "FAIL", b1);

        b2 = balloc();
        ok = (b2 != 0 && b2 != DISKFULL && b2 != b1);
        printf("balloc test #2: %s (block=%u, prev=%u)\n",
               ok ? "PASS" : "FAIL", b2, b1);

        /* free and re-allocate (LIFO stack) */
        bfree(b2);
        b3 = balloc();
        ok = (b3 == b2);
        printf("balloc/bfree roundtrip: %s (block=%u, expected=%u)\n",
               ok ? "PASS" : "FAIL", b3, b2);
    }

    /* --- Test iget/iput --- */
    {
        struct inode *ip;

        /* inode 1 should exist (zeroed on disk) and be loadable */
        ip = iget(1);
        ok = (ip != NULL);
        printf("iget/iput test: %s (ip=%p, i_ino=%u, i_count=%u)\n",
               ok ? "PASS" : "FAIL",
               (void *)ip,
               ip ? ip->i_ino : 0,
               ip ? ip->i_count : 0);
        if (ip) iput(ip);
    }

    /* --- Test ialloc/ifree --- */
    {
        struct inode *ip1, *ip2;
        unsigned int ino;

        ip1 = ialloc();
        ok = (ip1 != NULL);
        printf("ialloc test #1: %s (ip=%p, i_ino=%u)\n",
               ok ? "PASS" : "FAIL",
               (void *)ip1,
               ip1 ? ip1->i_ino : 0);

        if (ip1) {
            ino = ip1->i_ino;
            iput(ip1);

            /*
             * Allocate again.  The free list is LIFO, so we expect the
             * same inode number back (we just freed it).
             */
            ip2 = ialloc();
            ok = (ip2 != NULL && ip2->i_ino == ino);
            printf("ialloc/ifree roundtrip: %s (i_ino=%u, prev=%u)\n",
                   ok ? "PASS" : "FAIL",
                   ip2 ? ip2->i_ino : 0, ino);

            if (ip2) {
                /*
                 * Also test that we get a *different* inode the next time
                 * without an intervening ifree.
                 */
                struct inode *ip3 = ialloc();
                ok = (ip3 != NULL && ip3->i_ino != ino);
                printf("ialloc sequential: %s (i_ino=%u, prev=%u)\n",
                       ok ? "PASS" : "FAIL",
                       ip3 ? ip3->i_ino : 0, ino);
                if (ip3) iput(ip3);

                iput(ip2);
            }
        }
    }

    /*
     * --- Phase 2: B-layer integration tests ---
     * Test the newly ported high-level operations.
     */
    puts("\n=== integration test start ===");

    /* set up a root directory */
    init_root_dir();
    ok = (cur_path_inode != NULL);
    printf("root dir init: %s\n", ok ? "PASS" : "FAIL");

    /* --- Test namei / iname --- */
    {
        unsigned int ino;
        unsigned short idx;

        /* create a file entry for namei to find */
        idx = iname("testfile");
        ok = (idx > 0);
        printf("iname test: %s (idx=%u)\n", ok ? "PASS" : "FAIL", idx);

        if (ok) {
            dir.direct[idx].d_ino = 2;  /* inode 2 */
            dir.size++;

            ino = namei("testfile");
            ok = (ino == 2);
            printf("namei test: %s (ino=%u)\n", ok ? "PASS" : "FAIL", ino);

            ino = namei("nonexistent");
            ok = (ino == 0);
            printf("namei not-found: %s\n", ok ? "PASS" : "FAIL");
        }
    }

    /* --- Test creat / fs_write / fs_read / close --- */
    {
        int cfd;
        const char *wmsg = "Hello, filesystem!";
        char rbuf[64];

        cfd = creat("myfile");
        ok = (cfd >= 1);
        printf("creat test: %s (fd=%d)\n", ok ? "PASS" : "FAIL", cfd);

        if (ok) {
            unsigned int nw = fs_write(cfd, wmsg, (unsigned int)strlen(wmsg));
            ok = (nw == strlen(wmsg));
            printf("fs_write test: %s (wrote %u bytes)\n",
                   ok ? "PASS" : "FAIL", nw);

            /* reset offset for read */
            sys_ofile[user[user_id].u_ofile[cfd]].f_off = 0;

            memset(rbuf, 0, sizeof(rbuf));
            nw = fs_read(cfd, rbuf, (unsigned int)strlen(wmsg));
            ok = (nw == strlen(wmsg) && strcmp(rbuf, wmsg) == 0);
            printf("fs_read test: %s (read %u bytes: \"%s\")\n",
                   ok ? "PASS" : "FAIL", nw, rbuf);

            close(cfd);
            printf("close test: PASS\n");
        }
    }

    /* --- Test aopen (re-open) --- */
    {
        unsigned short fd;
        char rbuf[64];

        fd = aopen("myfile", FREAD);
        if (fd == 0) {
            printf("aopen test: FAIL (fd=0, access denied)\n");
            ok = 0;
        } else {
            unsigned int nr = fs_read(fd, rbuf, 6);
            ok = (nr == 6 && strncmp(rbuf, "Hello,", 6) == 0);
            printf("aopen+read test: %s (read \"%.6s\")\n",
                   ok ? "PASS" : "FAIL", rbuf);
            close(fd);
        }
    }

    /* --- Test mkdir / chdir --- */
    {
        mkdir("subdir");
        {
            unsigned int ino = namei("subdir");
            ok = (ino != 0);
            printf("mkdir test: %s (ino=%u)\n", ok ? "PASS" : "FAIL", ino);

            if (ok) {
                struct inode *sub = iget(ino);
                ok = (sub && (sub->di_mode & DIDIR));
                printf("mkdir is-dir: %s\n", ok ? "PASS" : "FAIL");
                if (sub) iput(sub);
            }
        }

        chdir("subdir");
        ok = (cur_path_inode != NULL);
        printf("chdir test: %s\n", ok ? "PASS" : "FAIL");
    }

    /* --- Test _dir --- */
    _dir();

    /* --- Test delete --- */
    {
        delete("myfile");
        ok = (namei("myfile") == 0);
        printf("delete test: %s\n", ok ? "PASS" : "FAIL");
    }

    /* --- Test login / logout --- */
    {
        int r;

        /* set up a test user entry (uid=1, gid=1, name="test", pw="test") */
        pwd[0].p_uid = 1;
        pwd[0].p_gid = 1;
        strcpy(pwd[0].p_name, "test");
        strcpy(pwd[0].password, "test");

        r = login("test", "wrong");
        ok = (r == 0);
        printf("login bad-pw: %s\n", ok ? "PASS" : "FAIL");

        r = login("test", "test");
        ok = (r == 1);
        printf("login good: %s (user_id=%d, uid=%u)\n",
               ok ? "PASS" : "FAIL", user_id, user[user_id].u_uid);

        logout();
        ok = (user[0].u_uid == 0);
        printf("logout test: %s\n", ok ? "PASS" : "FAIL");
    }

    halt();
    return 0;
}
