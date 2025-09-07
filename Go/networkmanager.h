#ifndef NETWORKMANAGER_H
#define NETWORKMANAGER_H

#include <QObject>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QJsonObject>

class NetworkManager : public QObject
{
    Q_OBJECT
public:
    explicit NetworkManager(QObject *parent = nullptr);
    ~NetworkManager();

    // 主机模式
    bool startHost(quint16 port);
    void stopHost();

    // 客户端模式
    void connectToHost(const QUrl &url);
    void disconnectFromHost();

    bool isConnected() const { return m_socket && m_socket->state() == QAbstractSocket::ConnectedState; }
    bool isHosting() const { return m_isHosting; }

    // 发送 JSON 对象 (序列化)
    void sendJson(const QJsonObject &obj);

signals:
    // 连接成功时触发 (主机接受客户端, 或客户端连接到主机时)
    void connected();
    void disconnected();
    void jsonReceived(const QJsonObject &obj);
    void logMessage(const QString &msg);

public slots:
    void emitJsonReceived(const QJsonObject &obj);

private slots:
    void onNewConnection();
    void onSocketTextMessageReceived(const QString &message);
    void onSocketDisconnected();

private:
    QWebSocketServer *m_server = nullptr;
    QWebSocket *m_socket = nullptr; // 本示例中为单点对等连接
    bool m_isHosting = false;
};

#endif // NETWORKMANAGER_H```
