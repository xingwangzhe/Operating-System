/**
 * =========================================================================
 * rdwt.c — 文件读写模块（任务B：文件操作层）
 * =========================================================================
 *
 * 本模块实现文件数据的读取和写入，是 B 层最复杂的模块。
 * 涉及跨块读写、非对齐偏移处理、按需块分配等逻辑。
 *
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │                    任务A（底层）依赖关系                              │
 * ├─────────────────────────────────────────────────────────────────────┤
 * │ 依赖项            │ 定义位置          │ 说明                          │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ balloc()           │ src/block.c:88    │ 任务A：从空闲块栈(s_free[])  │
 * │                    │                   │ 分配一个数据块，返回块索引    │
 * │                    │                   │ 栈空时从磁盘链块重新填充      │
 * │                    │                   │ 磁盘满返回 DISKFULL=65535     │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ fd (全局FILE*)     │ src/globals.c:8   │ 任务A：磁盘镜像文件的文件指针 │
 * │                    │                   │ 由 format()/install() 打开    │
 * │                    │                   │ 所有磁盘I/O通过fseek+fread/   │
 * │                    │                   │ fwrite 操作此文件              │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ sys_ofile[]        │ src/globals.c:14  │ 任务A：系统打开文件表         │
 * │                    │                   │ f_off: 当前读写偏移量(字节)   │
 * │                    │                   │ f_inode: 指向文件inode        │
 * │                    │                   │ f_flag: 读写模式标志          │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ user[]             │ src/globals.c:20  │ 任务A：用户表                 │
 * │                    │                   │ u_ofile[fd] → sys_ofile 索引  │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ user_id            │ src/globals.c:26  │ 任务A：当前用户索引           │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ BLOCKSIZ 常量      │ include/filesys.h │ 值=512，每块字节数            │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ DATASTART 常量     │ include/filesys.h │ 值=(2+32)*512=17408           │
 * │                    │                   │ 数据区起始字节偏移            │
 * │                    │                   │ di_addr[i] 存的是数据块索引   │
 * │                    │                   │ 磁盘位置 = DATASTART +        │
 * │                    │                   │           di_addr[i]*BLOCKSIZ │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ NADDR 常量         │ include/filesys.h │ 值=10，直接索引块数上限       │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ NOFILE/SYSOPENFILE │ include/filesys.h │ fd 合法性检查                 │
 * ├────────────────────┼───────────────────┼──────────────────────────────┤
 * │ DISKFULL 常量      │ include/filesys.h │ 值=65535，balloc 失败标志     │
 * └────────────────────┴───────────────────┴──────────────────────────────┘
 *
 * 磁盘寻址说明（重要！）：
 *   inode->di_addr[k] 存储的是"数据块索引"（0~FILEBLK-1），
 *   而非绝对块号。转换为磁盘字节偏移的公式：
 *     磁盘偏移 = DATASTART + di_addr[k] * BLOCKSIZ
 *   其中 DATASTART = (2 + DINODEBLK) * BLOCKSIZ = 17408
 *
 *   例如：di_addr[0] = 5 表示数据区的第5块，
 *         磁盘位置 = 17408 + 5 * 512 = 19968 字节处
 *
 * 命名说明：
 *   - 函数名从 read/write 改为 fs_read/fs_write，避免与 libc 的 POSIX 函数冲突
 *   - 参数名从 fd 改为 fildes，避免与全局 FILE *fd（磁盘镜像文件指针）冲突
 *     （原始 bug：本地 int fd 遮蔽了全局 FILE *fd，导致磁盘 I/O 操作错误的文件）
 */

#include "filesys.h"   /* 所有结构体、常量、全局变量声明 */
#include <stdio.h>     /* printf(), fseek(), fread(), fwrite() */

/**
 * fs_read — 从文件中读取数据
 * @fildes: 用户文件描述符（1~NOFILE-1）
 * @buf:    输出缓冲区（调用者分配）
 * @size:   期望读取的字节数
 * @return: 实际读取的字节数，失败返回 0
 *
 * 读取流程：
 *   1) 通过两级映射 fildes → sys_ofile → inode
 *   2) 根据当前偏移量 f_off 计算起始块和块内偏移
 *   3) 分三段处理：首个非完整块 → 连续完整块 → 末尾非完整块
 *   4) 每段都检查 di_addr[] 是否有效（非0），防止读取未分配区域
 *   5) 更新 f_off（偏移量前进）
 */
/**
 * fs_read — 从文件中读取数据
 * @fildes: 用户文件描述符（1~NOFILE-1）
 * @buf:    输出缓冲区（调用者分配）
 * @size:   期望读取的字节数
 * @return: 实际读取的字节数，失败返回 0
 *
 * 读取流程：通过 bmap() 将逻辑块号转换为物理块号，不再直接索引 di_addr[]。
 * bmap() 自动处理直接块 → 一次间接 → 二次间接 → 三次间接的逐级寻址。
 */
unsigned int fs_read(int fildes, char *buf, unsigned int size) {
    unsigned long off;                       /* 当前读写偏移量 */
    unsigned int block_off;                  /* 块内偏移（0~BLOCKSIZ-1） */
    unsigned int block_idx;                  /* 逻辑块号（传给 bmap） */
    int i, nblocks;                          /* i: 循环变量, nblocks: 完整块数量 */
    struct inode *inode;                     /* 文件 inode */
    unsigned int sys_no;                     /* 系统打开文件表索引 */
    char *dst;                               /* 目标写入指针 */
    unsigned int blkno;                      /* bmap() 返回的物理块号 */

    /* ── 参数合法性检查 ── */
    if (fildes < 0 || fildes >= NOFILE) return 0;

    /* ── 两级映射：用户fd → 系统打开文件表 ── */
    sys_no = user[user_id].u_ofile[fildes];
    if (sys_no == SYSOPENFILE + 1 || sys_no >= SYSOPENFILE) return 0;

    /* ── 检查文件是否以读模式打开 ── */
    if (!(sys_ofile[sys_no].f_flag & FREAD)) {
        printf("\n文件未以读模式打开\n");
        return 0;
    }

    /* ── 获取 inode ── */
    inode = sys_ofile[sys_no].f_inode;
    if (!inode) return 0;

    /* ── 计算实际可读字节数 ── */
    off = sys_ofile[sys_no].f_off;
    if (off + size > inode->di_size)
        size = (unsigned int)(inode->di_size - off);
    if (size == 0) return 0;

    /* ── 计算起始位置 ── */
    dst = buf;
    block_off = (unsigned int)(off % BLOCKSIZ);
    block_idx = (unsigned int)(off / BLOCKSIZ);

    /* ═══════════════════════════════════════════════════════════════════
     * 阶段一：读取第一个非完整块（如果 block_off > 0）
     * ═══════════════════════════════════════════════════════════════════ */
    if (block_off > 0) {
        unsigned int chunk = BLOCKSIZ - block_off;
        if (chunk > size) chunk = size;

        blkno = bmap(inode, block_idx, 0);           /* 读路径：create=0 */
        if (blkno == 0 || blkno == DISKFULL) return (unsigned int)(dst - buf);

        fseek(fd, DATASTART + (long)blkno * BLOCKSIZ + block_off, SEEK_SET);
        fread(dst, 1, chunk, fd);
        dst += chunk;
        size -= chunk;
        block_idx++;
    }

    /* ═══════════════════════════════════════════════════════════════════
     * 阶段二：读取中间的完整块（每次读 BLOCKSIZ=512 字节）
     * ═══════════════════════════════════════════════════════════════════ */
    nblocks = (int)(size / BLOCKSIZ);
    for (i = 0; i < nblocks; i++) {
        blkno = bmap(inode, block_idx + i, 0);       /* 读路径：create=0 */
        if (blkno == 0 || blkno == DISKFULL) break;

        fseek(fd, DATASTART + (long)blkno * BLOCKSIZ, SEEK_SET);
        fread(dst, 1, BLOCKSIZ, fd);
        dst += BLOCKSIZ;
        size -= BLOCKSIZ;
    }

    /* ═══════════════════════════════════════════════════════════════════
     * 阶段三：读取最后一个非完整块（如果还有剩余字节）
     * ═══════════════════════════════════════════════════════════════════ */
    if (size > 0) {
        blkno = bmap(inode, block_idx + nblocks, 0); /* 读路径：create=0 */
        if (blkno != 0 && blkno != DISKFULL) {
            fseek(fd, DATASTART + (long)blkno * BLOCKSIZ, SEEK_SET);
            fread(dst, 1, size, fd);
            dst += size;
        }
    }

    /* ── 更新文件偏移量 ── */
    sys_ofile[sys_no].f_off += (unsigned long)(dst - buf);

    return (unsigned int)(dst - buf);
}

/**
 * fs_write — 向文件中写入数据
 * @fildes: 用户文件描述符（1~NOFILE-1）
 * @buf:    输入缓冲区（要写入的数据）
 * @size:   要写入的字节数
 * @return: 实际写入的字节数，失败返回 0
 *
 * 写入流程：
 *   1) 通过两级映射获取 inode
 *   2) 确保起始块已分配（未分配则调用 balloc）
 *   3) 分三段处理：首个非完整块 → 连续完整块 → 末尾非完整块
 *   4) 每段在写之前检查并分配所需的数据块
 *   5) 更新 f_off 和 di_size
 *
 * 与 fs_read 的关键区别：写入时需要按需分配新块（balloc），
 * 而读取时仅检查块是否已分配。
 */
/**
 * fs_write — 向文件中写入数据
 * @fildes: 用户文件描述符（1~NOFILE-1）
 * @buf:    输入缓冲区（要写入的数据）
 * @size:   要写入的字节数
 * @return: 实际写入的字节数，失败返回 0
 *
 * 写入流程：通过 bmap(create=1) 将逻辑块号转换为物理块号，
 * 遇到空洞时自动分配新块（balloc）。bmap() 处理所有间接层。
 */
unsigned int fs_write(int fildes, const char *buf, unsigned int size) {
    unsigned long off;                       /* 当前读写偏移量 */
    unsigned int block_off;                  /* 块内偏移 */
    unsigned int block_idx;                  /* 逻辑块号（传给 bmap） */
    int i, nblocks;                          /* i:循环变量, nblocks:完整块数 */
    struct inode *inode;                     /* 文件 inode */
    unsigned int sys_no;                     /* 系统打开文件表索引 */
    unsigned int blkno;                      /* bmap() 返回的物理块号 */
    const char *src;                         /* 源数据指针 */
    unsigned int total_written;              /* 累计写入字节数 */

    /* ── 参数合法性检查 ── */
    if (fildes < 0 || fildes >= NOFILE) return 0;

    /* ── 两级映射 ── */
    sys_no = user[user_id].u_ofile[fildes];
    if (sys_no == SYSOPENFILE + 1 || sys_no >= SYSOPENFILE) return 0;

    /* ── 写权限检查 ── */
    if (!(sys_ofile[sys_no].f_flag & FWRITE)) {
        printf("\n文件未以写模式打开\n");
        return 0;
    }

    /* ── 获取 inode ── */
    inode = sys_ofile[sys_no].f_inode;
    if (!inode) return 0;

    /* ── 初始化写入状态 ── */
    off  = sys_ofile[sys_no].f_off;
    src  = buf;
    total_written = 0;
    block_off = (unsigned int)(off % BLOCKSIZ);
    block_idx = (unsigned int)(off / BLOCKSIZ);

    /* ═══════════════════════════════════════════════════════════════════
     * 阶段一：写入第一个非完整块
     * ═══════════════════════════════════════════════════════════════════ */
    if (block_off > 0) {
        unsigned int chunk = BLOCKSIZ - block_off;
        if (chunk > size) chunk = size;

        blkno = bmap(inode, block_idx, 1);           /* 写路径：create=1 */
        if (blkno == DISKFULL || blkno == 0) return total_written;

        fseek(fd, DATASTART + (long)blkno * BLOCKSIZ + block_off, SEEK_SET);
        fwrite(src, 1, chunk, fd);
        src += chunk;
        size -= chunk;
        total_written += chunk;
        block_idx++;
    }

    /* ═══════════════════════════════════════════════════════════════════
     * 阶段二：写入中间的完整块（每块 BLOCKSIZ=512 字节）
     * ═══════════════════════════════════════════════════════════════════ */
    nblocks = (int)(size / BLOCKSIZ);
    for (i = 0; i < nblocks; i++) {
        blkno = bmap(inode, block_idx + i, 1);       /* 写路径：create=1 */
        if (blkno == DISKFULL || blkno == 0) {
            sys_ofile[sys_no].f_off += total_written;
            return total_written;
        }

        fseek(fd, DATASTART + (long)blkno * BLOCKSIZ, SEEK_SET);
        fwrite(src, 1, BLOCKSIZ, fd);
        src += BLOCKSIZ;
        size -= BLOCKSIZ;
        total_written += BLOCKSIZ;
    }

    /* ═══════════════════════════════════════════════════════════════════
     * 阶段三：写入最后一个非完整块
     * ═══════════════════════════════════════════════════════════════════ */
    if (size > 0) {
        blkno = bmap(inode, block_idx + nblocks, 1); /* 写路径：create=1 */
        if (blkno != DISKFULL && blkno != 0) {
            fseek(fd, DATASTART + (long)blkno * BLOCKSIZ, SEEK_SET);
            fwrite(src, 1, size, fd);
            total_written += size;
        }
    }

    /* ── 更新文件状态 ── */
    sys_ofile[sys_no].f_off += total_written;
    if (sys_ofile[sys_no].f_off > inode->di_size)
        inode->di_size = sys_ofile[sys_no].f_off;     /* di_size 已是 unsigned long */

    return total_written;
}
