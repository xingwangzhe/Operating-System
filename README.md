# Operating-System — Unix V6 风格文件系统模拟器

操作系统课程项目。用 C99 实现一个完整的 Unix V6 风格文件系统，包含块分配、inode 管理、文件/目录操作和交互式 Shell。

## 快速开始

### 环境要求

- **Windows** + **MSYS2 UCRT64** 终端
- GCC（C99）+ GNU Make

### 安装工具链

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-make
ln -sf /ucrt64/bin/mingw32-make.exe /ucrt64/bin/make.exe
```

### 编译 & 运行

```bash
# 编译
make clean && make

# 跑测试（24 项全部 PASS）
./build/fs.exe --test

# 进入交互式 Shell
./build/fs.exe

# 直接用 gcc（不需要 make）
gcc -std=c99 -Wall -Wextra -Iinclude src/*.c src/B/*.c src/C/*.c -o build/fs.exe
```

### 持久化验证

文件系统数据保存在 `build/filesystem.img`，退出后重新运行数据不丢失：

```bash
# 第一次运行：创建文件
./build/fs.exe
# fs> format
# fs> creat hello.txt
# fs> write 1 "hello world"
# fs> halt

# 重新运行：install 恢复，数据仍在
./build/fs.exe
# fs> install
# fs> cat hello.txt
# hello world
```

### 调试模式

编译时加 `-DFS_DEBUG` 开启调试输出：

```bash
gcc -std=c99 -Wall -Wextra -DFS_DEBUG -Iinclude src/*.c src/B/*.c src/C/*.c -o build/fs.exe
```

---

## 交互式 Shell 使用

启动后输入命令操作文件系统。数据自动持久化到 `build/filesystem.img`，重新运行后 `install` 即可恢复：

```
fs> install               # 加载已有文件系统（接续上次数据）
fs> format                # 创建新的文件系统
fs> pwd                   # 显示当前路径
fs> dir                   # 列出当前目录
fs> ls -l                 # 列出目录（详细信息）
fs> mkdir testdir         # 创建子目录
fs> chdir testdir         # 切换目录
fs> creat myfile          # 创建文件
fs> write 1 "hello"       # 写入文件
fs> cat myfile            # 查看文件内容
fs> cp myfile copy.txt    # 复制文件
fs> mv copy.txt renamed.txt # 移动/重命名文件
fs> find "myfile"         # 搜索文件
fs> grep "hello" myfile   # 在文件中搜索字符串
fs> ln myfile hardlink    # 创建硬链接
fs> login root root       # 用户登录（获得属主权限）
fs> logout                # 用户注销（降为最低权限）
fs> rmdir testdir         # 删除空目录
fs> clear                 # 清屏
fs> halt                  # 退出
```

完整命令列表：

| 命令 | 功能 |
|------|------|
| `help` | 显示帮助信息 |
| `format` | 创建新的文件系统镜像 |
| `install` | 挂载已有的文件系统镜像 |
| `halt` | 卸载并退出 |
| `pwd` | 显示当前工作目录 |
| `dir` | 列出当前目录内容 |
| `ls -l` | 列出目录内容（详细信息） |
| `mkdir <name>` | 创建子目录 |
| `rmdir <name>` | 删除空目录 |
| `chdir <name>` | 切换目录 |
| `creat <name>` | 创建文件 |
| `open <name> [r\|w\|a]` | 打开文件 |
| `close <fd>` | 关闭文件描述符 |
| `read <fd> <nbytes>` | 从文件读取数据 |
| `write <fd> <text>` | 向文件写入数据 |
| `cat <name>` | 显示文件内容 |
| `cp <src> <dst>` | 复制文件 |
| `mv <src> <dst>` | 移动/重命名文件 |
| `ln <src> <dst>` | 创建硬链接 |
| `delete <name>` | 删除文件 |
| `find <pattern>` | 搜索文件 |
| `grep <pattern> <file>` | 在文件中搜索字符串 |
| `clear` | 清屏 |
| `login <uid> <pwd>` | 用户登录 |
| `logout` | 用户注销 |

---

## 项目结构

```
Operating-System/
├── include/
│   └── filesys.h         # 所有结构体、常量、函数声明
├── src/
│   ├── main.c            # 入口（--test 测试模式 / 交互 Shell）
│   ├── test.c            # 自测代码（24 项）
│   ├── globals.c         # 全局变量定义
│   ├── block.c           # 块分配/释放/读写（V6 链式空闲块）
│   ├── inode.c           # inode 分配/释放/iget/iput（哈希链）
│   ├── format.c          # 创建磁盘镜像 + 初始化根目录
│   ├── install.c         # 挂载已有磁盘镜像
│   ├── halt.c            # 关闭打开文件 → 写回超级块 → 关闭镜像
│   ├── name.c            # 目录中按名查找/存储（namei/iname）
│   ├── access.c          # 权限检查（user → group → other）
│   ├── dir.c             # 列目录、mkdir、chdir、路径管理
│   ├── log.c             # 用户登录/注销
│   ├── file.c            # 交互式命令 Shell
│   ├── B/                # B 层 - 文件操作
│   │   ├── open.c        # 打开文件（aopen）
│   │   ├── close.c       # 关闭文件
│   │   ├── creat.c       # 创建文件
│   │   ├── delete.c      # 删除文件
│   │   ├── name.c        # 路径名解析（namei/iname）
│   │   ├── access.c      # 权限检查
│   │   └── rdwt.c        # 文件读写（fs_read/fs_write）
│   └── C/                # C 层 - 用户工具
│       ├── pwd.c         # 显示当前路径
│       ├── rmdir.c       # 删除空目录
│       ├── cat.c         # 显示文件内容
│       ├── clear.c       # 清屏
│       ├── cp.c          # 文件复制
│       ├── mv.c          # 文件移动/重命名
│       ├── ls.c          # 详细列表（ls -l）
│       ├── find.c        # 文件搜索
│       ├── grep.c        # 内容搜索
│       └── ln.c          # 创建硬链接
├── orig/                 # 原始教师参考代码（不编译，有 30 处 bug）
├── build/
│   └── filesystem.img    # 磁盘镜像（运行时生成）
├── Makefile
├── .gitignore
└── README.md
```

---

## 磁盘布局

```
绝对块 0          → 引导块（未使用）
绝对块 1          → 超级块 (struct filsys)
绝对块 2..33      → inode 表（32 块，每块 16 个 dinode，共 512 个）
绝对块 34..545    → 数据区（512 块，每块 512 字节）
```

- `DATASTART = 17408`（字节偏移，指向第一个数据块）
- `s_free[]` 和 `di_addr[]` 存的是**数据块索引**（0..511），不是绝对块号
- `bread()`/`bwrite()` 吃的是**绝对块号**
- 磁盘镜像固定大小：279,552 字节（546 块 × 512）
- 最大文件：原来仅支持 10 个直接块（5 KB），现已支持**间接块寻址**（可撑满整盘 256 KB）

### di_addr 间接块寻址布局（C 层实现）

inode 的 `di_addr[10]` 采用 Unix V6 三级间接块策略：

| 槽位 | 类型 | 逻辑块范围 | 累计容量 |
|------|------|-----------|---------|
| `di_addr[0..6]` | 直接块 | 0 — 6 | 3.5 KB |
| `di_addr[7]` | **一次间接**（1 × 128 指针） | 7 — 134 | +64 KB |
| `di_addr[8]` | **二次间接**（128 × 128 指针） | 135 — 16510 | +8 MB |
| `di_addr[9]` | **三次间接**（128³ 指针） | 16511+ | +1 GB |

每个间接块存 128 个 `unsigned int` 指针（`512 ÷ 4 = 128`），写路径遇空洞自动分配链块。

相关函数由 C 层实现：`bmap()` — 逻辑块号→物理块号映射；`itrunc()` — 递归释放所有直接/间接块。

---

## FD（文件描述符）约定

| 函数 | 返回类型 | 失败值 | 有效值 |
|------|----------|--------|--------|
| `creat()` | `int` | -1 | fd ≥ 1 |
| `aopen()` | `unsigned short` | 0 | fd ≥ 1 |
| `close()` / `fs_read()` / `fs_write()` | — | — | 接受 1-based fd |

`user[].u_ofile[0]` 故意留空，让 fd=0 明确表示失败。

---

## 数据持久化

文件系统数据保存在磁盘镜像文件 `build/filesystem.img` 中，支持跨会话持久化。

### 存盘流程（`halt`）

```
halt()
  ├─ 关闭所有用户的打开文件描述符（→ iput() 写回每个 inode）
  ├─ sync_dir()     → 当前目录条目写回数据区
  ├─ iput(当前目录)  → 目录 inode 写回 inode 表
  ├─ fwrite(&filsys)→ 超级块写回块 1
  └─ fclose(fd)     → 关闭镜像文件
```

### 加载流程（`install`）

```
install(path)
  ├─ fopen(path, "r+b")  → 打开已有镜像
  ├─ fread(&filsys)      → 读取超级块（空闲栈信息）
  ├─ iget(1)             → 加载根目录 inode
  ├─ 读取目录块 → dir.direct[] 恢复当前目录
  └─ 设置 cur_path_inode → 恢复环境
```

### 支持动态镜像路径

`format` 和 `install` 可指定镜像文件路径（省略时默认 `build/filesystem.img`）：

```
fs> format /path/to/my.img
fs> install /path/to/my.img
```

---

## 权限系统

采用 Unix V6 风格 9 位权限（`rwx`），用于控制文件/目录的读、写、执行（目录为进入）权限。

### 权限位

```
 rwx  rwx  rwx
属主  同组  其他
```

| 八进制 | 二进制 | 含义 |
|--------|--------|------|
| `00400` | `r` | 属主读 |
| `00200` | `w` | 属主写 |
| `00100` | `x` | 属主执行 |
| `00040` | `r` | 同组读 |
| `00020` | `w` | 同组写 |
| `00010` | `x` | 同组执行 |
| `00004` | `r` | 其他读 |
| `00002` | `w` | 其他写 |
| `00001` | `x` | 其他执行 |

| 权限值 | 文件含义 | 目录含义 |
|--------|---------|---------|
| `r`（读） | 查看文件内容（cat） | 列出目录内容（dir） |
| `w`（写） | 修改文件内容（write） | 在目录中增删文件（creat/delete） |
| `x`（执行） | 执行文件 | 进入目录（chdir） |

> ⚠️ **Bug 修复：** 原始代码中 9 个权限宏全部反位（`UDIREAD=00001` 却在 ODI 的位置）。已按 POSIX 标准修正为属主 `00400/00200/00100`、同组 `00040/00020/00010`、其他 `00004/00002/00001`。

### 默认权限

- 宏 `DEFAULTMODE = 00755`（原为 `00777`）
- 新建文件/目录默认权限：**`rwxr-xr-x`**
  - 属主：读写+执行（`rwx`）
  - 同组：读+执行（`r-x`）
  - 其他：读+执行（`r-x`）

### 登录 vs 未登录

系统使用 `logged_in` 标志区分状态：

| 状态 | 权限检查范围 | 典型行为 |
|------|-------------|---------|
| **未登录**（默认） | 仅检查"其他用户(other)"权限位 | 只能读他人文件，不能写 |
| **已登录**（`login` 后） | 完整三层检查（其他→同组→属主） | 属主可写，他人按组/其他权限 |

- `login <用户名> <密码>` — 登录后获得属主身份，按完整权限检查
- `logout` — 注销后降级为最低权限（仅 other）

这意味着不登录时只能拥有最受限的权限，登录后才能根据文件属主获得相应的读写权限。

## 测试结果（24 项全 PASS）

```
=== smoke test start ===
bread/bwrite test:             PASS
balloc test #1:                PASS (block=511)
balloc test #2:                PASS (block=510, prev=511)
balloc/bfree roundtrip:        PASS (block=510, expected=510)
iget/iput test:                PASS
ialloc test #1:                PASS
ialloc/ifree roundtrip:        PASS (i_ino=4, prev=4)
ialloc sequential:             PASS (i_ino=5, prev=4)

=== integration test start ===
root dir init:                 PASS
iname test:                    PASS (idx=2)
namei test:                    PASS (ino=2)
namei not-found:               PASS
creat test:                    PASS (fd=1)
fs_write test:                 PASS (wrote 18 bytes)
fs_read test:                  PASS (read 18 bytes: "Hello, filesystem!")
close test:                    PASS
aopen+read test:               PASS (read "Hello,")
mkdir test:                    PASS (ino=5)
mkdir is-dir:                  PASS
chdir test:                    PASS
delete test:                   PASS
login bad-pw:                  PASS
login good:                    PASS (user_id=0, uid=1)
logout test:                   PASS
```

---

## 模块完成状态

| 层级 | 文件 | 功能 | 状态 |
|------|------|------|------|
| A 层 | `block.c` | 块分配/释放/读写 | ✅ |
| A 层 | `inode.c` | inode 分配/释放/iget/iput | ✅ 已升级 |
| A 层 | `format.c` | 创建磁盘镜像 + 根目录初始化 + 预设用户表 | ✅ |
| A 层 | `install.c` | 挂载已有磁盘镜像 | ✅ |
| A 层 | `halt.c` | 关闭打开文件 → 写回超级块 → 关闭镜像 | ✅ 已修复 |
| B 层 | `src/B/name.c` | 路径名解析（namei/iname） | ✅ |
| B 层 | `src/B/access.c` | 权限检查（含 logged_in 登录/未登录区分） | ✅ 已升级 |
| B 层 | `src/B/open.c` | 打开文件（aopen） | ✅ |
| B 层 | `src/B/close.c` | 关闭文件 | ✅ |
| B 层 | `src/B/creat.c` | 创建文件（C 层升级 itrunc 截断） | ✅ |
| B 层 | `src/B/delete.c` | 删除文件 | ✅ |
| B 层 | `src/B/rdwt.c` | 文件读写（C 层升级 bmap 间接块寻址） | ✅ |
| B 层 | `dir.c` | 列目录、mkdir、chdir、路径管理（C 层升级 bmap/itrunc） | ✅ |
| B 层 | `log.c` | 用户登录/注销（logged_in 状态联动） | ✅ 已升级 |
| B 层 | `globals.c` | 全局变量（含 logged_in 标志） | ✅ 已升级 |
| C 层 | `src/C/pwd.c` | 显示当前路径 | ✅ |
| C 层 | `src/C/rmdir.c` | 删除空目录（C 层升级 itrunc 释放） | ✅ |
| C 层 | `src/C/cat.c` | 显示文件内容 | ✅ |
| C 层 | `src/C/clear.c` | 清屏 | ✅ |
| C 层 | `src/C/cp.c` | 文件复制 | ✅ |
| C 层 | `src/C/mv.c` | 文件移动/重命名 | ✅ |
| C 层 | `src/C/ls.c` | 详细列表（ls -l，修复权限位显示顺序） | ✅ 已修复 |
| C 层 | `src/C/find.c` | 文件搜索（C 层升级 bmap 目录遍历） | ✅ |
| C 层 | `src/C/grep.c` | 内容搜索 | ✅ |
| C 层 | `src/C/ln.c` | 创建硬链接 | ✅ |
| C 层 | `include/filesys.h` | 扩展常量 + 修复权限宏定义 | ✅ 已修复 |
| C 层 | **`── 间接块子系统`** | **`bmap()` 块寻址 + `itrunc()` 块释放** | **✅ 新功能** |
| C 层 | `src/inode.c bmap/itrunc` | 跨 A 层植入间接块寻址引擎 | ✅ |
| Shell | `file.c` | 交互式命令 Shell | ✅ |
| 入口 | `main.c` | 测试模式 + 交互模式切换 | ✅ |

---

## 开发规范

- **分支策略**：功能分支 + PR，禁止直接 push 到 master
- **提交粒度**：每次只做一件事，每个 commit 必须能编译
- **提交格式**：`<type>: <简短描述>`
  - `feat:` — 新功能
  - `fix:` — 修 bug
  - `refactor:` — 重构
  - `chore:` — 清理、配置
  - `test:` — 测试相关
- **永远不要** force push 到 master
- **永远不要** 提交 `.o` / `.exe` / `.img` 等编译产物
- `orig/` 目录仅供参考，不对其编译

### 提交前检查清单

```bash
git status                  # 确认要提交的文件
make clean && make          # 确认编译通过
./build/fs.exe --test       # 确认 24 项全 PASS
git add src/xxx.c ...       # 只加源文件，别用 git add -A
git commit -m "fix: ..."
```

---

## 已知问题 & 待办

- [x] `halt()` 时仅 flush 了 inode 元数据，未 flush 目录数据块（已通过 `sync_dir()` 修复）
- [x] 部分操作（creat/delete）修改内存 dir 缓冲后，不立即写回磁盘（已修复缓存写入同步机制）
- [x] `aopen()` 的 append 模式实际行为是 truncate（设计意图待确认）
- [x] 磁盘镜像路径硬编码为 `build/filesystem.img`（已支持动态参数指定镜像路径）
- [x] 文件大小受限于 10 个直接块（5 KB）（已通过间接块寻址解决，最大可撑满整盘 256 KB）
- [x] `halt()` 未关闭打开的文件描述符，导致 inode 修改留在内存未写盘，重启后文件数据丢失（已修复：halt 时遍历所有用户的 u_ofile 逐一 iput 写回）
- [x] 权限常量 `UDIREAD`~`ODIEXICUTE` 9 个宏全部反位，导致权限检查逻辑失效（已按 POSIX 标准修正）
- [x] `ls -l` 权限位显示从 LSB 读到 MSB，顺序为"其他→同组→属主"（已改为从 MSB 到 LSB，正确显示 `rwxr-xr-x`）
- [x] 无预设用户，`login` 无法演示使用（已修复：format 时初始化 root/root、user/pass、test/1234 三个预设用户）
