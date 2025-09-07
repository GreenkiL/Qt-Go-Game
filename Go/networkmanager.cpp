#include "networkmanager.h"
#include <QJsonDocument>
#include <QHostAddress>

NetworkManager::NetworkManager(QObject *parent)
    : QObject(parent)
{
    // 注册元类型, 以支持在 QueuedConnection 模式下使用 QJsonObject 作为参数
    qRegisterMetaType<QJsonObject>("QJsonObject");
}


NetworkManager::~NetworkManager()
{
    if (m_isHosting) {
        stopHost();
    } else {
        disconnectFromHost();
    }
}

void NetworkManager::emitJsonReceived(const QJsonObject &obj)
{
    // 统一通过此函数发射信号, 确保所有接收方都在事件循环中异步处理
    emit jsonReceived(obj);
}

bool NetworkManager::startHost(quint16 port)
{
    if (m_server) return false;
    m_server = new QWebSocketServer(QStringLiteral("Go Demo Server"),
                                    QWebSocketServer::NonSecureMode, this);
    if (!m_server->listen(QHostAddress::Any, port)) {
        emit logMessage(QStringLiteral("无法监听端口 %1").arg(port));
        delete m_server;
        m_server = nullptr;
        return false;
    }
    connect(m_server, &QWebSocketServer::newConnection, this, &NetworkManager::onNewConnection);
    m_isHosting = true;
    emit logMessage(QStringLiteral("已启动 Host, 监听端口 %1").arg(port));
    return true;
}

void NetworkManager::stopHost()
{
    if (m_server) {
        if (m_socket) {
            disconnect(m_socket, nullptr, this, nullptr);
            m_socket->close();
            m_socket->deleteLater();
            m_socket = nullptr;
        }
        m_server->close();
        disconnect(m_server, nullptr, this, nullptr);
        delete m_server;
        m_server = nullptr;
        m_isHosting = false;
        emit logMessage(QStringLiteral("Host 已停止"));
    }
}

void NetworkManager::connectToHost(const QUrl &url)
{
    // 创建客户端套接字 (用于连接中央服务器)
    if (m_socket) {
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_socket = new QWebSocket();
    connect(m_socket, &QWebSocket::connected, this, [this]() {
        emit logMessage(QStringLiteral("已连接到服务器"));
        emit connected();
    });
    connect(m_socket, &QWebSocket::disconnected, this, &NetworkManager::onSocketDisconnected);
    connect(m_socket, &QWebSocket::textMessageReceived, this, &NetworkManager::onSocketTextMessageReceived);

    m_socket->open(url);
    m_isHosting = false;
}

void NetworkManager::disconnectFromHost()
{
    if (m_socket) {
        disconnect(m_socket, nullptr, this, nullptr);
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    emit disconnected();
}

void NetworkManager::onNewConnection()
{
    if (!m_server) return;
    if (m_socket) {
        QWebSocket *peer = m_server->nextPendingConnection();
        peer->close();
        peer->deleteLater();
        emit logMessage(QStringLiteral("拒绝额外连接 (仅支持1v1)"));
        return;
    }
    m_socket = m_server->nextPendingConnection();
    connect(m_socket, &QWebSocket::textMessageReceived, this, &NetworkManager::onSocketTextMessageReceived);
    connect(m_socket, &QWebSocket::disconnected, this, &NetworkManager::onSocketDisconnected);
    emit logMessage(QStringLiteral("有玩家连接 (Host已接受)"));
    emit connected();
}

void NetworkManager::onSocketTextMessageReceived(const QString &message)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        emit logMessage(QStringLiteral("收到非法的JSON数据"));
        return;
    }
    QJsonObject obj = doc.object();

    // 异步转发到事件队列, 避免在socket回调栈中直接处理业务逻辑, 防止重入崩溃
    // QueuedConnection 会将参数安全地放入事件循环, 由 emitJsonReceived 槽函数处理
    QMetaObject::invokeMethod(this, "emitJsonReceived", Qt::QueuedConnection, Q_ARG(QJsonObject, obj));
}


void NetworkManager::onSocketDisconnected()
{
    emit logMessage(QStringLiteral("对方断开连接"));
    // 避免重复释放
    if (!m_socket) return;
    disconnect(m_socket, nullptr, this, nullptr);
    m_socket->deleteLater();
    m_socket = nullptr;
    emit disconnected();
}

void NetworkManager::sendJson(const QJsonObject &obj)
{
    if (!m_socket) {
        emit logMessage(QStringLiteral("发送失败: 未连接"));
        return;
    }
    QJsonDocument doc(obj);
    QString txt = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    m_socket->sendTextMessage(txt);
}
