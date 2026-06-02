/**
 * =========================================================================
 * creat.c — 文件创建模块（任务B：文件操作层）
 * =========================================================================
 *
 * 本模块实现文件的创建（及已存在文件的截断），是 B 层最核心的模块之一。
 * 涉及 inode 分配、目录项创建、系统打开文件表分配、用户文件描述符分配。
 *
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │                    任务A（底层）依赖关系                              │
 * ├─────────────────────────────────────────────────────────────────────┤
 * │ 依赖项            │ 定义位置          │ 说明                          │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ namei(name)        │ src/B/name.c:46   │ 任务B：按文件名查inode号      │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ iname(name)        │ src/B/name.c:76   │ 任务B：找空目录项+写入文件名  │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ access(ino,mode)   │ src/B/access.c    │ 任务B：权限检查               │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ iget(di_ino)       │ src/inode.c:25    │ 任务A：通过inode号获取内存    │
 * │                    │                   │ inode，引用计数+1             │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ iput(inode)        │ src/inode.c:99    │ 任务A：释放inode引用          │
 * │                    │                   │ 计数归零时写回磁盘或释放资源  │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ ialloc()           │ src/inode.c:149   │ 任务A：从超级块inode空闲栈    │
 * │                    │                   │ 分配一个空闲磁盘inode         │
 * │                    │                   │ 返回已加载到内存的inode指针   │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ bfree(blkno)       │ src/block.c:142   │ 任务A：回收一个数据块到空闲   │
 * │                    │                   │ 块栈（s_free[]成组链接法）    │
 * │                    │                   │ 用于截断旧文件时释放数据块    │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ sys_ofile[]        │ src/globals.c:14  │ 任务A：系统打开文件表（40项） │
 * │ 全局数组           │                   │ sys_ofile[i].f_count: 引用计数│
 * │                    │                   │ sys_ofile[i].f_inode:→inode   │
 * │                    │                   │ sys_ofile[i].f_off:  偏移量   │
 * │                    │                   │ sys_ofile[i].f_flag: 读写标志  │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ user[] 全局数组    │ src/globals.c:20  │ 任务A：用户表                 │
 * │                    │                   │ user[id].u_ofile[j]:→sys索引  │
 * │                    │                   │ SYSOPENFILE+1 表示空闲        │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ user_id 全局变量   │ src/globals.c:26  │ 任务A：当前用户索引           │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ dir 全局变量       │ src/globals.c:23  │ 任务A：当前目录               │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ DIEMPTY/DIFILE/    │ include/filesys.h │ 任务A：类型/权限常量          │
 * │ DEFAULTMODE等常量  │                   │                               │
 * └────────────────────┴───────────────────┴──────────────────────────────┘
 *
 * 两级文件描述符设计（类比 Unix 的 fd 表 + 打开文件表）：
 *   user[user_id].u_ofile[fd]  ──索引──▶  sys_ofile[idx]
 *   (用户级fd, 0~NOFILE-1)               (系统级打开文件, 0~SYSOPENFILE-1)
 *   每个用户最多 NOFILE=20 个打开文件      全局最多 SYSOPENFILE=40 个打开文件
 */

#include "filesys.h"   /* 所有结构体、常量、全局变量声明 */
#include <stdio.h>     /* printf() */

/**
 * creat — 创建新文件（或截断已存在的文件）
 * @name: 文件名（在当前目录中）
 * @return: 用户文件描述符（>=1），失败返回 -1
 *
 * 完整流程：
 *   1) namei() 检查文件是否存在
 *   2a) 存在 → 权限检查 → 释放旧数据块(bfree) → 重置 → 分配fd
 *   2b) 不存在 → 分配inode(ialloc) → 创建目录项(iname) → 分配fd
 *
 * 关于"截断"（truncate）：
 *   当文件已存在时，creat 会释放该文件的所有数据块（调用 bfree），
 *   并将 di_size 重置为 0，保留原有的 inode 号和权限信息。
 *   这与 Unix creat(2) 的语义一致。
 */
int creat(const char *name) {
    unsigned int di_ino;                     /* namei() 返回的 inode 号 */
    struct inode *inode;                     /* 内存 inode 指针 */
    int i, j;                                /* i: 通用循环变量, j: 目录/fd 索引 */

    /* ═══════════════════════════════════════════════════════════════════
     * 阶段一：检查文件是否已存在
     * ═══════════════════════════════════════════════════════════════════ */
    di_ino = namei(name);                    /* [任务B] 调用 namei 在当前目录中查找 */
                                             /* namei 遍历 dir.direct[]，按名匹配 */
                                             /* 返回 d_ino（inode号），未找到返回 0 */

    if (di_ino != 0) {                       /* ─── 文件已存在 → 截断 ─── */
        /* 步骤1: 获取内存 inode */
        inode = iget(di_ino);                /* [任务A] 通过 inode 号获取内存 inode */
                                             /* iget 先在 hinode 哈希链中查找，未命中则从磁盘读取 */
        if (!inode) return -1;               /* iget 失败（磁盘错误等），返回 -1 */

        /* 步骤2: 权限检查 — 只有具备写权限才能截断 */
        if (!access(inode->i_ino, WRITE)) {  /* [任务B] 检查当前用户对文件的写权限 */
            printf("\ncreat: 访问被拒绝\n"); /* 权限不足，拒绝操作 */
            iput(inode);                     /* [任务A] 释放 inode 引用 */
            return -1;
        }

        /* 步骤3: 释放旧文件的所有数据块 */
        /* itrunc() 会遍历并释放所有直接块 + 间接块（单/双/三） */
        itrunc(inode);

        /* 步骤4: 重置文件大小 */
        inode->di_size = 0;                  /* 截断后文件大小为 0 字节 */

        /* 步骤5: 重置所有打开此文件实例的偏移量 */
        /* 如果有其他进程/用户打开了此文件，将它们的偏移量也重置 */
        for (i = 0; i < SYSOPENFILE; i++)    /* 遍历系统打开文件表（最多40项） */
            if (sys_ofile[i].f_inode == inode) /* 找到指向同一 inode 的打开实例 */
                sys_ofile[i].f_off = 0;      /* 将偏移量重置为文件开头 */

        /* 步骤6: 分配用户文件描述符和系统打开文件表槽位 */
        /* 从 fd=1 开始（fd=0 保留用于表示"失败"） */
        for (i = 1; i < NOFILE; i++) {       /* NOFILE=20，每个用户最多20个打开文件 */
            /* 检查该用户fd槽位是否空闲：SYSOPENFILE+1(=41) 表示未使用 */
            if (user[user_id].u_ofile[i] == SYSOPENFILE + 1) {

                /* 找到空闲的用户fd槽位后，分配一个系统打开文件表槽位 */
                for (j = 0; j < SYSOPENFILE; j++)  /* 遍历 sys_ofile[] */
                    if (sys_ofile[j].f_count == 0) break;  /* f_count==0 表示该槽位空闲 */
                if (j == SYSOPENFILE) {      /* 系统打开文件表已满（40个全在用） */
                    iput(inode);             /* [任务A] 释放 inode */
                    return -1;
                }

                /* 建立映射：用户fd[i] → 系统打开文件[j] */
                user[user_id].u_ofile[i] = (unsigned short)j;  /* 用户fd指向系统打开文件索引 */
                sys_ofile[j].f_flag  = FWRITE;      /* 截断后以写模式打开（FWRITE=00002） */
                sys_ofile[j].f_count = 1;           /* 引用计数=1（仅此用户打开） */
                sys_ofile[j].f_off   = 0;           /* 偏移量从0开始 */
                sys_ofile[j].f_inode = inode;       /* 指向文件 inode */
                /* 更新当前用户的 uid/gid 以匹配文件属主 */
                user[user_id].u_uid  = inode->di_uid;  /* 文件属主ID */
                user[user_id].u_gid  = inode->di_gid;  /* 文件属主组ID */
                return i;                         /* 返回用户文件描述符 */
            }
        }
        /* 用户打开文件表已满（20个全在用） */
        iput(inode);                             /* [任务A] 释放 inode */
        return -1;
    }

    /* ═══════════════════════════════════════════════════════════════════
     * 阶段二：文件不存在 → 创建新文件
     * ═══════════════════════════════════════════════════════════════════ */

    /* 步骤1: 分配一个新的磁盘 inode */
    inode = ialloc();                        /* [任务A] 从超级块 inode 空闲栈分配 */
                                             /* ialloc 从 filsys.s_inode[] 栈顶弹出 inode 号 */
                                             /* 若栈空则扫描磁盘 inode 表重新填充 */
                                             /* 返回已加载到内存的 inode 指针（引用计数=1） */
    if (!inode) return -1;                   /* inode 用尽或磁盘错误 */

    /* 步骤2: 在目录中找空槽位并存入文件名 */
    j = (int)iname(name);                    /* [任务B] 遍历 dir.direct[] 找 d_ino==0 的槽位 */
                                             /* 找到后将文件名 strcpy 到 dir.direct[j].d_name */
    if (j == 0 && dir.size >= DIRNUM) {      /* j==0 且目录已满(DIRNUM=128)→真的满了 */
        printf("\ncreat: 目录已满\n");       /* 目录最多容纳128个条目 */
        iput(inode);                         /* [任务A] 回滚：释放刚分配的 inode */
        return -1;
    }

    /* 步骤3: 设置目录项的 inode 号 */
    dir.direct[j].d_ino = inode->i_ino;      /* 将目录项指向新分配的 inode */
    dir.size++;                              /* 目录有效条目数 +1 */

    /* 步骤4: 初始化 inode 字段 */
    inode->di_mode   = user[user_id].u_default_mode | DIFILE;  /* 权限位：默认权限 | 普通文件标志 */
                                                                 /* DIFILE=01000(八进制)，标记为普通文件 */
                                                                 /* u_default_mode 初始=DEFAULTMODE=00777 */
    inode->di_uid    = user[user_id].u_uid;  /* 文件属主 = 当前用户ID */
    inode->di_gid    = user[user_id].u_gid;  /* 文件属组 = 当前用户组ID */
    inode->di_size   = 0;                    /* 新文件大小为 0 */
    inode->di_number = 1;                    /* 硬链接计数 = 1（一个目录项指向此inode） */
                                             /* 当 di_number 减到 0 时，iput() 会释放所有资源和 inode */

    /* 步骤5: 分配系统打开文件表槽位（从索引 0 开始扫描） */
    for (i = 0; i < SYSOPENFILE; i++)        /* 遍历 sys_ofile[] */
        if (sys_ofile[i].f_count == 0) break; /* f_count==0 表示空闲 */
    if (i == SYSOPENFILE) {                  /* 全部40个槽位都在使用 */
        iput(inode);                         /* [任务A] 回滚 */
        return -1;
    }

    /* 步骤6: 分配用户文件描述符（从 1 开始，0 保留表示失败） */
    for (j = 1; j < NOFILE; j++)             /* 遍历当前用户的 u_ofile[] */
        if (user[user_id].u_ofile[j] == SYSOPENFILE + 1) break;  /* SYSOPENFILE+1=41 表示空闲 */
    if (j == NOFILE) {                       /* 用户打开文件数达到上限(20) */
        iput(inode);                         /* [任务A] 回滚 */
        return -1;
    }

    /* 步骤7: 建立两级映射并返回文件描述符 */
    user[user_id].u_ofile[j] = (unsigned short)i;  /* 用户fd[j] → 系统打开文件[i] */
    sys_ofile[i].f_flag  = FREAD | FWRITE;  /* 新建文件以读写模式打开 */
    sys_ofile[i].f_count = 1;               /* 引用计数=1 */
    sys_ofile[i].f_off   = 0;               /* 偏移量=0（文件开头） */
    sys_ofile[i].f_inode = inode;           /* 指向文件 inode */

    /* 同步目录至磁盘，防止未 chdir 就 halt 导致丢失 */
    sync_dir();

    return j;                                /* 返回用户文件描述符 */
}
