#pragma once

#include <QString>
#include <QByteArray>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <libssh2.h>
#include <libssh2_sftp.h>

class SshWorker
{
public:
    SshWorker();
    ~SshWorker();

    // 长连接：连接/断开
    bool connectTo(const QString& host, int port,
                   const QString& user, const QString& pass,
                   int timeoutMs = 10000);
    void disconnect();
    bool isConnected() const;

    // 可选：心跳检测（用于判断是否还活着）
    bool ping();

    // 复用连接：执行命令
    QByteArray exec(const QString& cmd);

    // 复用连接：SFTP 上传/下载
    bool upload(const QString& localPath, const QString& remotePath);
    bool download(const QString& remotePath, const QString& localPath);

    // 工具：确保远端目录存在（例如 /root/FZKJ
    bool ensureRemoteDir(const QString& remoteDir);

private:
#ifdef _WIN32
    SOCKET sock_ = INVALID_SOCKET;
#else
    int sock_ = -1;
#endif
    LIBSSH2_SESSION* session_ = nullptr;
    LIBSSH2_SFTP* sftp_ = nullptr;

    // 保存连接参数，方便断线重连
    QString host_, user_, pass_;
    int port_ = 22;
    int timeoutMs_ = 10000;

private:
    bool ensureConnected();        // 若断开则自动重连一次
    void cleanupSftp();
    void cleanupSession();

#ifdef _WIN32
    bool wsaInited_ = false;
    bool wsaStartupOnce();
    void wsaCleanupOnce();
    SOCKET tcpConnect(const char* host, int port);
#endif
};
