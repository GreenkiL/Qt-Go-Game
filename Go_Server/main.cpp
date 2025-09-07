#include <QCoreApplication>
#include "GameServer.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    GameServer server;

    QString err;

    if (!server.initAuthDB("127.0.0.1", 3306, "go_server", "user", "password", err)) {
        qWarning() << "数据库连接失败：" << err;
        return 1;
    }

    if (!server.startServer(12345)) {
        qWarning() << "无法启动 GameServer";
        return 1;
    }
    return a.exec();
}
