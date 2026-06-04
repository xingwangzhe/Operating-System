/**
 * =========================================================================
 * access.c — 文件访问权限检查模块（任务B：文件操作层）
 * =========================================================================
 *
 * 本模块实现 Unix V6 风格的 9 位权限检查（属主/同组/其他 × 读/写/执行）。
 * 在 creat/aopen/mkdir/chdir 等操作前调用，确保当前用户有相应权限。
 *
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │                    任务A（底层）依赖关系                              │
 * ├─────────────────────────────────────────────────────────────────────┤
 * │ 依赖项            │ 定义位置          │ 说明                          │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ iget(ino)          │ src/inode.c:25    │ 任务A：通过inode号获取内存   │
 * │                    │                   │ inode。先在hinode哈希链中查找 │
 * │                    │                   │ 未命中则从磁盘读取dinode      │
 * │                    │                   │ 返回引用计数+1的inode指针     │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ iput(ip)           │ src/inode.c:99    │ 任务A：释放inode引用。引用    │
 * │                    │                   │ 计数减1；归零时写回磁盘或     │
 * │                    │                   │ 释放数据块+归还inode          │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ user[] 全局数组    │ src/globals.c:20  │ 任务A：用户表，记录每个登录   │
 * │                    │                   │ 用户的uid/gid/打开文件表      │
 * │                    │                   │ user[user_id].u_uid 当前用户ID│
 * │                    │                   │ user[user_id].u_gid 当前组ID  │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ user_id 全局变量   │ src/globals.c:26  │ 任务A：当前登录用户的索引     │
 * │                    │                   │ 由 login() 设置，logout()清零 │
 * └────────────────────┴───────────────────┴──────────────────────────────┘
 *
 * 权限常量（include/filesys.h，八进制表示）：
 *   UDIREAD=00001 属主读    GDIREAD=00010 同组读    ODIREAD=00100 其他读
 *   UDIWRITE=00002属主写    GDIWRITE=00020同组写    ODIWRITE=00200其他写
 *   UDIEXICUTE=00004属主执行 GDIEXICUTE=00040同组执行 ODIEXICUTE=00400其他执行
 *
 * 权限检查顺序：其他用户 → 同组用户 → 文件属主（从宽到严）
 */

#include "filesys.h"   /* 所有结构体、权限常量、全局变量声明 */

/**
 * check_access — 内部权限检查函数（static，仅本文件可见）
 * @inode: 指向已加载到内存的 inode（通过 iget() 获取）
 * @mode:  请求的访问模式（READ=1 / WRITE=2 / EXICUTE=3）
 * @return: 1=有权限，0=无权限
 *
 * 检查逻辑：按"其他→同组→属主"顺序，逐级放宽。
 *   - 如果文件对其他用户开放该权限 → 直接放行（最宽松）
 *   - 如果当前用户与文件属主同组且同组权限开放 → 放行
 *   - 如果当前用户是文件属主且属主权限开放 → 放行
 *   - 都不满足 → 拒绝
 */
static int check_access(struct inode *inode, unsigned short mode) {
    /* 未登录：仅检查"其他用户(other)"权限位，不查同组/属主 */
    if (!logged_in) {
        switch (mode) {
        case READ:   return (inode->di_mode & ODIREAD)   ? 1 : 0;
        case WRITE:  return (inode->di_mode & ODIWRITE)  ? 1 : 0;
        case EXICUTE: return (inode->di_mode & ODIEXICUTE) ? 1 : 0;
        default:     return 0;
        }
    }

    /* 已登录：完整的三层检查（其他→同组→属主，从宽到严） */
    switch (mode) {                          /* 根据请求的访问类型分派 */

    case READ:                               /* ─── 读权限检查 ─── */
        /* di_mode 的低 9 位存储权限位（八进制 000~777） */
        /* ODIREAD=00100：位与运算检查"其他用户读"权限位 */
        if (inode->di_mode & ODIREAD) return 1;

        /* GDIREAD=00010：检查"同组用户读"权限位 */
        /* 条件：同组权限位已设置 且 当前用户的组ID == 文件的组ID */
        /* user[user_id].u_gid 是任务A全局变量，记录当前登录用户的组ID */
        /* inode->di_gid 是文件创建时记录的属主组ID */
        if ((inode->di_mode & GDIREAD) &&
            user[user_id].u_gid == inode->di_gid) return 1;

        /* UDIREAD=00001：检查"文件属主读"权限位 */
        /* 条件：属主权限位已设置 且 当前用户的用户ID == 文件的用户ID */
        if ((inode->di_mode & UDIREAD) &&
            user[user_id].u_uid == inode->di_uid) return 1;

        return 0;                            /* 三层检查都不通过，拒绝 */

    case WRITE:                              /* ─── 写权限检查 ─── */
        /* 逻辑与 READ 完全对称，仅权限位不同 */
        if (inode->di_mode & ODIWRITE) return 1;  /* 其他用户可写？ */
        if ((inode->di_mode & GDIWRITE) &&
            user[user_id].u_gid == inode->di_gid) return 1;  /* 同组用户可写？ */
        if ((inode->di_mode & UDIWRITE) &&
            user[user_id].u_uid == inode->di_uid) return 1;  /* 文件属主可写？ */
        return 0;

    case EXICUTE:                            /* ─── 执行/进入权限检查 ─── */
        /* 对目录而言，"执行"权限意味着可以进入（chdir）该目录 */
        if (inode->di_mode & ODIEXICUTE) return 1;
        if ((inode->di_mode & GDIEXICUTE) &&
            user[user_id].u_gid == inode->di_gid) return 1;
        if ((inode->di_mode & UDIEXICUTE) &&
            user[user_id].u_uid == inode->di_uid) return 1;
        return 0;

    default:                                 /* 未知访问模式 */
        /* 原始 bug：这里拼写为 "defualt"，已修复为 "default" */
        return 0;                            /* 拒绝未定义的访问模式 */
    }
}

/**
 * access — 对外权限检查接口
 * @ino:  磁盘 inode 号
 * @mode: 请求的访问模式（READ=1 / WRITE=2 / EXICUTE=3）
 * @return: 1=有权限，0=无权限
 *
 * 调用链：access() → iget(ino) [任务A] → check_access() → iput() [任务A]
 *
 * 调用者（creat/open/chdir 等）先通过 namei() 获取 inode 号，
 * 再调用本函数检查权限，然后决定是否继续操作。
 */
unsigned int access(unsigned int ino, unsigned int mode) {
    struct inode *inode;                     /* 内存 inode 指针 */
    unsigned int result;                     /* 权限检查结果 */

    /* 步骤1: 通过 inode 号获取内存 inode（调用任务A的 iget()） */
    /* iget() 先在 hinode[] 哈希链中查找，未命中则从磁盘 dinode 表读取 */
    /* 返回的 inode 引用计数 +1，必须用 iput() 配对释放 */
    inode = iget(ino);                       /* [任务A] src/inode.c:25 */
    if (!inode) return 0;                    /* iget 失败（磁盘I/O错误等），拒绝 */

    /* 步骤2: 执行权限检查 */
    result = check_access(inode, (unsigned short)mode);

    /* 步骤3: 释放 inode 引用（调用任务A的 iput()） */
    /* iput() 递减引用计数；若计数归零：di_number!=0时写回磁盘， */
    /* di_number==0时释放所有数据块并归还 inode */
    iput(inode);                             /* [任务A] src/inode.c:99 */

    return result;                           /* 返回检查结果给调用者 */
}
