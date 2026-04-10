#include "ssh_worker.h"

#include <QFile>
#include <QDebug>

SshWorker::SshWorker() = default;

SshWorker::~SshWorker()
{
    disconnect();
}

bool SshWorker::connectTo(const QString& host, int port,
                          const QString& user, const QString& pass,
                          int timeoutMs)
{
    host_ = host;
    port_ = port;
    user_ = user;
    pass_ = pass;
    timeoutMs_ = timeoutMs;

    if (isConnected())
        return true;

#ifdef _WIN32
    if (!wsaStartupOnce())
        return false;

    sock_ = tcpConnect(host.toUtf8().constData(), port);
    if (sock_ == INVALID_SOCKET) {
        qWarning() << "TCP connect failed";
        return false;
    }
#endif

    session_ = libssh2_session_init();
    if (!session_) {
        qWarning() << "libssh2_session_init failed";
        cleanupSession();
        return false;
    }

    libssh2_session_set_timeout(session_, timeoutMs_);

    int rc = libssh2_session_handshake(session_, (int)sock_);
    if (rc) {
        qWarning() << "handshake failed rc=" << rc;
        cleanupSession();
        return false;
    }

    rc = libssh2_userauth_password(session_,
                                   user_.toUtf8().constData(),
                                   pass_.toUtf8().constData());
    if (rc) {
        qWarning() << "auth failed rc=" << rc;
        cleanupSession();
        return false;
    }

    // SFTP 句柄延迟初始化：第一次 upload/download 才 init
    return true;
}

void SshWorker::disconnect()
{
    cleanupSftp();
    cleanupSession();
#ifdef _WIN32
    wsaCleanupOnce();
#endif
}

bool SshWorker::isConnected() const
{
    return session_ != nullptr;
}

bool SshWorker::ensureConnected()
{
    if (isConnected())
        return true;

    // 简单自动重连一次
    return connectTo(host_, port_, user_, pass_, timeoutMs_);
}

bool SshWorker::ping()
{
    // 最简单心跳：exec echo
    auto out = exec("echo PING");
    return out.contains("PING");
}

QByteArray SshWorker::exec(const QString& cmd)
{
    if (!ensureConnected())
        return {};

    LIBSSH2_CHANNEL* channel = libssh2_channel_open_session(session_);
    if (!channel) {
        qWarning() << "channel_open_session failed";
        return {};
    }

    libssh2_channel_handle_extended_data2(channel, LIBSSH2_CHANNEL_EXTENDED_DATA_MERGE);

    int rc = libssh2_channel_exec(channel, cmd.toUtf8().constData());
    if (rc) {
        qWarning() << "channel_exec failed rc=" << rc;
        libssh2_channel_free(channel);
        return {};
    }

    QByteArray out;
    char buf[4096];
    for (;;) {
        ssize_t n = libssh2_channel_read(channel, buf, sizeof(buf));
        if (n > 0) out.append(buf, (int)n);
        else break; // 0=EOF 或 <0=error
    }

    libssh2_channel_close(channel);
    libssh2_channel_free(channel);
    return out;
}

bool SshWorker::ensureRemoteDir(const QString& remoteDir)
{
    // 最省事：直接用命令创建（root 登录时最好用）
    // remoteDir 例如：/root/FZKJ
    QByteArray out = exec(QString("mkdir -p \"%1\"").arg(remoteDir));
    Q_UNUSED(out);
    return isConnected(); // 若 mkdir 执行导致断线，isConnected 可能变 false
}

bool SshWorker::upload(const QString& localPath, const QString& remotePath)
{
    if (!ensureConnected())
        return false;

    if (!sftp_) {
        sftp_ = libssh2_sftp_init(session_);
        if (!sftp_) {
            qWarning() << "libssh2_sftp_init failed";
            return false;
        }
    }

    LIBSSH2_SFTP_HANDLE* h = libssh2_sftp_open(
        sftp_,
        remotePath.toUtf8().constData(),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        0644
        );
    if (!h) {
        qWarning() << "libssh2_sftp_open(write) failed:" << remotePath;
        return false;
    }

    QFile f(localPath);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "open local file failed:" << localPath;
        libssh2_sftp_close(h);
        return false;
    }

    while (!f.atEnd()) {
        QByteArray chunk = f.read(32 * 1024);
        const char* p = chunk.constData();
        ssize_t left = chunk.size();
        while (left > 0) {
            ssize_t n = libssh2_sftp_write(h, p, (size_t)left);
            if (n < 0) {
                qWarning() << "libssh2_sftp_write failed:" << (int)n;
                f.close();
                libssh2_sftp_close(h);
                return false;
            }
            p += n;
            left -= n;
        }
    }

    f.close();
    libssh2_sftp_close(h);
    return true;
}

bool SshWorker::download(const QString& remotePath, const QString& localPath)
{
    if (!ensureConnected())
        return false;

    if (!sftp_) {
        sftp_ = libssh2_sftp_init(session_);
        if (!sftp_) {
            qWarning() << "libssh2_sftp_init failed";
            return false;
        }
    }

    LIBSSH2_SFTP_HANDLE* h = libssh2_sftp_open(
        sftp_,
        remotePath.toUtf8().constData(),
        LIBSSH2_FXF_READ,
        0
        );
    if (!h) {
        qWarning() << "libssh2_sftp_open(read) failed:" << remotePath;
        return false;
    }

    QFile f(localPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "open local file for write failed:" << localPath;
        libssh2_sftp_close(h);
        return false;
    }

    char buf[32 * 1024];
    for (;;) {
        ssize_t n = libssh2_sftp_read(h, buf, sizeof(buf));
        if (n > 0) {
            if (f.write(buf, (qint64)n) != (qint64)n) {
                qWarning() << "write local file failed";
                f.close();
                libssh2_sftp_close(h);
                return false;
            }
        } else {
            break; // 0=EOF, <0=error
        }
    }

    f.close();
    libssh2_sftp_close(h);
    return true;
}

void SshWorker::cleanupSftp()
{
    if (sftp_) {
        libssh2_sftp_shutdown(sftp_);
        sftp_ = nullptr;
    }
}

void SshWorker::cleanupSession()
{
    if (session_) {
        libssh2_session_disconnect(session_, "bye");
        libssh2_session_free(session_);
        session_ = nullptr;
    }

#ifdef _WIN32
    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }
#endif
}

#ifdef _WIN32
bool SshWorker::wsaStartupOnce()
{
    if (wsaInited_) return true;
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        qWarning() << "WSAStartup failed";
        return false;
    }
    wsaInited_ = true;
    return true;
}

void SshWorker::wsaCleanupOnce()
{
    if (!wsaInited_) return;
    WSACleanup();
    wsaInited_ = false;
}

SOCKET SshWorker::tcpConnect(const char* host, int port)
{
    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    char portStr[16];
    snprintf(portStr, sizeof(portStr), "%d", port);

    if (getaddrinfo(host, portStr, &hints, &res) != 0 || !res)
        return INVALID_SOCKET;

    SOCKET sock = INVALID_SOCKET;
    for (addrinfo* p = res; p; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock == INVALID_SOCKET) continue;
        if (connect(sock, p->ai_addr, (int)p->ai_addrlen) == 0) break;
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    return sock;
}
#endif
