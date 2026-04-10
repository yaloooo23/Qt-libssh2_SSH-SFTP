因为自己看了网上很多都跑不通，然后自己研究了一上午把这个做出来。
适用场景
- Windows 上使用 Qt（MinGW 64） 开发上位机/工具
- 通过 SSH 登录 Linux 设备（用户名/密码）
- 执行远端命令、使用 SFTP 上传/下载文件
- 不依赖系统 ssh/scp，可用于正式产品

0. 最终效果（你要达到什么）
✔ Qt 程序在 Windows 上运行
✔ 使用 libssh2 + OpenSSL 建立 SSH 连接
✔ 通过 root/root（或其他账号）登录 Linux
✔ 执行命令（等同 ssh root@ip）
✔ 使用 SFTP 上传/下载文件（如 /root/FZKJ）
1. 整体技术路径（先看这个，理解全局）
Qt 程序（Windows, MinGW）
    │
    ├─ WinSock 建立 TCP 连接（22端口）
    │
    ├─ libssh2 （SSH 协议）
    │     ├─ SSH 握手 / 加密协商
    │     ├─ 用户名 + 密码认证
    │     ├─ Channel exec（命令）
    │     └─ SFTP 子系统（文件传输）
    │
    └─ Linux 设备（SSH Server ）

关键认知：
- SSH 是 应用层协议，必须跑在 TCP 上
- libssh2 不负责建网络连接，只负责 SSH 协议

2. 环境前提
2.1 Windows 侧
Windows 10 / 11
Qt MinGW 64-bit 套件
Qt Creator（qmake 或 CMake 均可，本手册用 qmake）
2.2 设备侧（Linux）
已开启 SSH 服务（sshd）
确认可登录：
ssh root@设备IP
3. 安装 MSYS2（MinGW 生态，核心步骤）
3.1 安装 MSYS2
官网：https://www.msys2.org/
安装完成后会得到目录: C:\msys64
3.2 打开 MSYS2 MinGW 64-bit 终端
⚠️ 注意：必须是 MinGW64，不是 UCRT、不是 MSYS

终端提示应类似：

MINGW64 ~

3.3 更新系统
pacman -Syu

如提示关闭终端 → 关闭 → 重新打开 → 再执行一次
4. 安装 libssh2 + OpenSSL（MinGW 64）
pacman -S mingw-w64-x86_64-libssh2 mingw-w64-x86_64-openssl

安装完成后：

头文件：C:\msys64\mingw64\include
导入库（链接期）：C:\msys64\mingw64\lib
运行时 DLL：C:\msys64\mingw64\bin
5. 在 Qt 项目中准备第三方库（推荐做法）
5.1 项目内目录结构
sshtest/
├─ third_party/
│  └─ libssh2/
│     ├─ include/
│     │  ├─ libssh2.h
│     │  ├─ libssh2_sftp.h
│     │  ├─ libssh2_publickey.h
│     │  ├─ zlib.h
│     │  └─ zconf.h
│     └─ lib/
│        └─ libssh2.dll.a

说明：
- *.h：编译期需要
- libssh2.dll.a：链接期需要
- 不依赖 MSYS2  安装路径，项目可移植

5.2 .pro 文件配置
INCLUDEPATH += $$PWD/third_party/libssh2/include
LIBS += -L$$PWD/third_party/libssh2/lib -lssh2
LIBS += -lws2_32

-lws2_32 是 Windows TCP（WinSock）必须的

6. 运行时 DLL 放置（非常重要）
6.1 需要的 DLL
（这是因为我后面要打包整个文件，所以就想把ddl放在同一个文件目录下，其实你自己C:\msys64\mingw64\bin里的也可以直接用）

从：

C:\msys64\mingw64\bin

复制到 exe 同目录：

libssh2-1.dll
libssl-3-x64.dll
libcrypto-3-x64.dll
zlib1.dll

6.2 放置位置示例
build/release/
├─ sshtest.exe
├─ libssh2-1.dll
├─ libssl-3-x64.dll
├─ libcrypto-3-x64.dll
├─ zlib1.dll

7. 验证 libssh2 是否可用（最小测试）
#include <libssh2.h>
#include <QDebug>

int main () {
    qDebug() << "libssh2_init rc =" << libssh2_init(0);
    qDebug() << "libssh2 version =" << libssh2_version(0);
    libssh2_exit();
}

输出示例：

libssh2_init rc = 0
libssh2 version = 1.11.1

8. SSH 执行命令（核心功能）
8.1 关键流程
WinSock socket + connect(22)

libssh2_session_init

libssh2_session_handshake

libssh2_userauth_password

libssh2_channel_exec

读取输出

8.2 实测效果
uname -a
id
ls /

等同于终端：

ssh root@设备IP

9. SFTP 文件传输（本实验重点）
9.1 核心原则（一定要记住）
SFTP 必须复用已登录成功的 SSH session
❌ 不能重新建 session 
✔ 一个 session = exec + sftp 都可用

10. SFTP 上传文件（示例）
10.1 上传到 /root/FZKJ
sshExec(c, "mkdir -p /root/FZKJ");

bool ok = sftpUpload(
    c,
    "D:/test.txt",
    "/root/FZKJ/test.txt"
);

10.2 验证
ls -l /root/FZKJ/test.txt
md5sum /root/FZKJ/test.txt

11. SFTP 下载文件（示例）
bool ok = sftpDownload(
    c,
    "/root/FZKJ/test.txt",
    "D:/download_test.txt"
);

12. 实验结果验证（你已跑通）
upload  ok = true
-rw-r--r-- 1 root root ... /root/FZKJ/test.txt
md5 正确

download ok = true
————————————————
版权声明：本文为CSDN博主「Fan_____42271」的原创文章，遵循CC 4.0 BY-SA版权协议，转载请附上原文出处链接及本声明。
原文链接：https://blog.csdn.net/2401_82842271/article/details/156907699
