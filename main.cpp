#include <libssh2.h>
#include <QDebug>
#include "ssh_worker.h"

int main(int argc, char *argv[])
{
    Q_UNUSED(argc);
    Q_UNUSED(argv);

    qDebug() << "libssh2_init rc =" << libssh2_init(0);
    qDebug() << "libssh2 version =" << libssh2_version(0);

    SshWorker w;
    if (!w.connectTo("192.168.123.123", 22, "root123", "root123")) {
        qWarning() << "connect failed";
        libssh2_exit();
        return 1;
    }

    w.ensureRemoteDir("/root/FZKJ");

    bool upOk = w.upload("D:/test.txt", "/root/FZKJ/test.txt");
    qDebug() << "upload ok =" << upOk;

    qDebug().noquote() << w.exec("ls -l /root/FZKJ/test.txt; md5sum /root/FZKJ/test.txt || true");

    bool downOk = w.download("/root/FZKJ/test.txt", "D:/download_test.txt");
    qDebug() << "download ok =" << downOk;

    qDebug() << "ping ok =" << w.ping();

    w.disconnect();
    libssh2_exit();
    return 0;
}
