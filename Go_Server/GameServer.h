#ifndef GAMESERVER_H
#define GAMESERVER_H

#include <QObject>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QMap>
#include <QQueue>

#include "authmanager.h"

// 玩家数据结构
struct Player {
    QString id;         // 唯一标识符
    int userId = 0;     // 数据库用户ID
    QString username;   // 用户名
    QString nickname;   // 昵称
    int rating = 1200;  // 段位/积分
    int wins = 0;       // 胜场
    int losses = 0;     // 负场

    QWebSocket* socket = nullptr; // WebSocket 连接指针
    QString roomId;     // 所在房间ID
    bool isBlack = false; // 是否执黑
    bool ready = false;   // 是否已准备
};

// 房间数据结构
struct Room {
    QString id;         // 房间ID
    Player* p1 = nullptr; // 玩家1
    Player* p2 = nullptr; // 玩家2
    bool p1Ready = false; // 玩家1是否准备
    bool p2Ready = false; // 玩家2是否准备
};

class GameServer : public QObject
{
    Q_OBJECT
public:
    explicit GameServer(QObject* parent = nullptr);
    ~GameServer();

    // 初始化认证数据库
    bool initAuthDB(const QString &dbHost, int dbPort,
                    const QString &dbName, const QString &dbUser,
                    const QString &dbPassword, QString &errMsg);
    // 启动服务器
    bool startServer(quint16 port);
    // 停止服务器
    void stopServer();

private slots:
    // 处理新连接
    void onNewConnection();
    // 处理收到的文本消息
    void onTextMessageReceived(const QString &message);
    // 处理连接断开
    void onSocketDisconnected();

private:
    // 处理匹配请求
    void handleMatch(Player* pl);
    // 当双方准备好时尝试开始游戏
    void tryStartWhenReady(Room* room);
    // 向指定玩家发送消息
    void sendToPlayer(Player* pl, const QJsonObject &obj);
    // 向指定玩家的对手发送消息
    void sendToOpponent(Player* pl, const QJsonObject &obj);
    // 向指定玩家发送房间列表
    void sendRoomListToPlayer(Player* pl);
    // 获取玩家所在的房间
    Room* getRoomForPlayer(Player* pl);
    // 广播房间列表给所有玩家
    void broadcastRoomList();
    // 为两个玩家创建房间
    QString createRoomForTwo(Player* a, Player* b);
    // 处理创建房间请求 (手动)
    void handleCreateRoom(Player* pl);
    // 处理加入房间请求 (手动)
    void handleJoinRoom(Player* pl, const QString &roomId);

private:
    QWebSocketServer* m_server = nullptr;
    // WebSocket 到 Player 对象的映射
    QMap<QWebSocket*, Player*> m_map;
    // 房间ID 到 Room 对象的映射
    QMap<QString, Room*> m_rooms;
    // 等待匹配的玩家队列
    QQueue<Player*> m_waiting;
    // 房间计数器, 用于生成房间ID
    int m_roomCounter = 0;

    AuthManager m_auth;
};

#endif // GAMESERVER_H
