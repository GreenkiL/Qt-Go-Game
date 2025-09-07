#include "GameServer.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QDebug>
#include <QJsonArray>
#include <QDateTime>

GameServer::GameServer(QObject *parent) : QObject(parent)
{
    m_server = new QWebSocketServer(QStringLiteral("GoCentralServer"),
                                    QWebSocketServer::NonSecureMode, this);
}

GameServer::~GameServer()
{
    stopServer();
}

bool GameServer::initAuthDB(const QString &dbHost, int dbPort,
                            const QString &dbName,
                            const QString &dbUser,
                            const QString &dbPassword,
                            QString &errMsg)
{
    return m_auth.openDatabase(dbHost, dbPort, dbName, dbUser, dbPassword, errMsg);
}

bool GameServer::startServer(quint16 port)
{
    if (!m_server) return false;
    if (!m_server->listen(QHostAddress::Any, port)) {
        qWarning() << "无法监听端口" << port;
        return false;
    }
    connect(m_server, &QWebSocketServer::newConnection, this, &GameServer::onNewConnection);
    qDebug() << "游戏服务器已启动, 端口:" << port;
    return true;
}

void GameServer::stopServer()
{
    if (!m_server) return;
    m_server->close();
    disconnect(m_server, &QWebSocketServer::newConnection, this, &GameServer::onNewConnection);

    // 清理所有玩家连接和数据
    for (auto it = m_map.begin(); it != m_map.end(); ++it) {
        QWebSocket* sock = it.key();
        Player* pl = it.value();
        if (sock) { sock->close(); sock->deleteLater(); }
        delete pl;
    }
    m_map.clear();

    // 清理所有房间数据
    for (auto it = m_rooms.begin(); it != m_rooms.end(); ++it) {
        delete it.value();
    }
    m_rooms.clear();
    while (!m_waiting.isEmpty()) m_waiting.dequeue();
}

void GameServer::onNewConnection()
{
    QWebSocket* sock = m_server->nextPendingConnection();
    Player* pl = new Player;
    pl->id = QUuid::createUuid().toString();
    pl->socket = sock;
    pl->roomId.clear();
    pl->isBlack = false;
    pl->ready = false;
    pl->userId = 0;

    m_map.insert(sock, pl);

    connect(sock, &QWebSocket::textMessageReceived, this, &GameServer::onTextMessageReceived);
    connect(sock, &QWebSocket::disconnected, this, &GameServer::onSocketDisconnected);

    qDebug() << "新玩家连接:" << pl->id << sock->peerAddress().toString();
}

void GameServer::onTextMessageReceived(const QString &message)
{
    QWebSocket* sock = qobject_cast<QWebSocket*>(sender());
    if (!sock || !m_map.contains(sock)) return;
    Player* pl = m_map[sock];

    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        sendToPlayer(pl, QJsonObject{{"type","error"},{"msg","非法的JSON格式"}});
        return;
    }
    QJsonObject obj = doc.object();
    QString type = obj.value("type").toString();

    // 注册
    if (type == "register") {
        QString username = obj.value("username").toString();
        QString password = obj.value("password").toString();
        QString nickname = obj.value("nickname").toString();
        QJsonObject ret = m_auth.registerUser(username, password, nickname);
        QJsonObject resp;
        resp["type"] = "register_result";
        for (auto it = ret.begin(); it != ret.end(); ++it) resp[it.key()] = it.value();
        sendToPlayer(pl, resp);
        return;
    }

    // 登录
    if (type == "login") {
        QString username = obj.value("username").toString();
        QString password = obj.value("password").toString();
        QJsonObject ret = m_auth.loginUser(username, password);
        QJsonObject resp;
        resp["type"] = "login_result";
        if (!ret.value("success").toBool()) {
            resp["success"] = false;
            resp["msg"] = ret.value("msg").toString();
            sendToPlayer(pl, resp);
            return;
        }
        // 登录成功: 检查此账号是否已在别处登录 (防多开)
        QJsonObject user = ret.value("user").toObject();
        int incomingId = user.value("id").toInt();
        for (auto it = m_map.begin(); it != m_map.end(); ++it) {
            Player* other = it.value();
            if (other == pl) continue;
            if (other && other->userId == incomingId) {
                // 发现重复登录, 拒绝本次请求
                resp["success"] = false;
                resp["msg"] = QStringLiteral("该账号已在其他设备登录");
                sendToPlayer(pl, resp);
                return;
            }
        }

        // 填充玩家信息
        pl->userId = user.value("id").toInt();
        pl->username = username;
        pl->nickname = user.value("nickname").toString();
        pl->rating = user.value("rating").toInt();
        pl->wins = user.value("wins").toInt();
        pl->losses = user.value("losses").toInt();

        resp["success"] = true;
        resp["user"] = user;
        sendToPlayer(pl, resp);

        // 登录后立即发送当前房间列表
        sendRoomListToPlayer(pl);
        return;
    }


    // 退出登录
    if (type == "logout") {
        pl->userId = 0;
        pl->username.clear();
        pl->nickname.clear();
        pl->rating = 1200;
        pl->wins = pl->losses = 0;
        sendToPlayer(pl, QJsonObject{{"type","logout_result"},{"success",true}});
        return;
    }

    // 手动创建房间
    if (type == "create_room") {
        if (pl->userId == 0) {
            sendToPlayer(pl, QJsonObject{{"type","error"},{"msg","请先登录"}});
            return;
        }
        handleCreateRoom(pl);
        broadcastRoomList();
        return;
    }

    // 手动加入房间
    if (type == "join_room") {
        if (pl->userId == 0) {
            sendToPlayer(pl, QJsonObject{{"type","error"},{"msg","请先登录"}});
            return;
        }
        QString rid = obj.value("room_id").toString();
        if (rid.isEmpty()) {
            sendToPlayer(pl, QJsonObject{{"type","error"},{"msg","缺少 room_id"}});
            return;
        }
        handleJoinRoom(pl, rid);
        broadcastRoomList();
        return;
    }

    // 请求房间列表
    if (type == "list_rooms") {
        sendRoomListToPlayer(pl);
        return;
    }

    // 匹配
    if (type == "match") {
        if (pl->userId == 0) {
            sendToPlayer(pl, QJsonObject{{"type","error"},{"msg","请先登录"}});
            return;
        }
        handleMatch(pl);
        return;
    }

    if (type == "cancel_match") {
        // 从等待队列中移除玩家
        m_waiting.removeAll(pl);
        // 回应客户端取消成功
        sendToPlayer(pl, QJsonObject{{"type","match_cancelled"}, {"msg","已取消匹配"}});
        qDebug() << "玩家取消匹配:" << (pl->username.isEmpty() ? QString::number(pl->userId) : pl->username);
        return;
    }

    Room* room = getRoomForPlayer(pl);

    // 聊天消息
    if (type == "chat") {
        QString text = obj.value("text").toString();
        if (!room) {
            sendToPlayer(pl, QJsonObject{{"type","error"},{"msg","未在房间内, 无法聊天"}} );
            return;
        }
        QJsonObject out;
        out["type"] = "chat";
        out["text"] = text;
        out["from"] = pl->nickname.isEmpty() ? pl->username : pl->nickname;
        out["user_id"] = pl->userId;
        out["room_id"] = room->id;
        out["time"] = QDateTime::currentDateTime().toString(Qt::ISODate);

        // 广播给房间内的所有玩家 (包括自己)
        if (room->p1 && room->p1->socket) sendToPlayer(room->p1, out);
        if (room->p2 && room->p2->socket && room->p2 != room->p1) sendToPlayer(room->p2, out);
        return;
    }

    // 准备
    if (type == "ready") {
        if (!room) {
            sendToPlayer(pl, QJsonObject{{"type","error"},{"msg","未在房间内"}}); return;
        }
        if (room->p1 == pl) room->p1Ready = true;
        if (room->p2 == pl) room->p2Ready = true;
        sendToOpponent(pl, QJsonObject{{"type","player_ready"}});
        tryStartWhenReady(room);
        return;
    }

    // 离开房间
    if (type == "leave") {
        if (room) {
            Player* other = (room->p1 == pl ? room->p2 : room->p1);
            if (other) {
                sendToPlayer(other, QJsonObject{{"type","opponent_left"},{"room_id", room->id}});
                other->roomId.clear();
            }
            m_rooms.remove(room->id);
            delete room;
            broadcastRoomList();
        }
        pl->roomId.clear();
        pl->ready = false;
        return;
    }

    // 游戏内消息转发 (落子, 虚着, 认输等)
    if (type == "move" || type == "pass" || type == "turn" ||
        type == "resign" || type == "sync" || type == "newgame" )
    {
        if (!room) {
            sendToPlayer(pl, QJsonObject{{"type","error"},{"msg","未在房间内"}}); return;
        }
        if (type == "resign") {
            sendToOpponent(pl, obj);
            room->p1Ready = false;
            room->p2Ready = false;
            // 更新数据库战绩
            Player* other = (room->p1 == pl ? room->p2 : room->p1);
            if (pl->userId != 0) { QString e; m_auth.updateResult(pl->userId, false, e); }
            if (other && other->userId != 0) { QString e; m_auth.updateResult(other->userId, true, e); }
            broadcastRoomList(); // 广播房间列表以更新段位显示
            return;
        }
        sendToOpponent(pl, obj);
        return;
    }

    // 点目请求/确认/拒绝
    if (type == "end_request") {
        if (!room) { sendToPlayer(pl, QJsonObject{{"type","error"},{"msg","未在房间内"}}); return; }
        sendToOpponent(pl, obj);
        return;
    }
    if (type == "end_confirm") {
        if (!room) { sendToPlayer(pl, QJsonObject{{"type","error"},{"msg","未在房间内"}}); return; }
        // 广播给双方
        sendToOpponent(pl, obj);
        sendToPlayer(pl, obj);

        // 可选: 如果客户端在消息中附加了胜负方ID, 则更新数据库
        if (obj.contains("winner_userid")) {
            int wid = obj.value("winner_userid").toInt();
            int lid = obj.value("loser_userid").toInt();
            QString e;
            if (wid != 0) m_auth.updateResult(wid, true, e);
            if (lid != 0) m_auth.updateResult(lid, false, e);
        }
        room->p1Ready = false;
        room->p2Ready = false;
        broadcastRoomList();
        return;
    }
    if (type == "end_decline") {
        if (!room) { sendToPlayer(pl, QJsonObject{{"type","error"},{"msg","未在房间内"}}); return; }
        sendToOpponent(pl, obj);
        return;
    }

    sendToPlayer(pl, QJsonObject{{"type","error"},{"msg","未知消息类型:" + type}});
}

void GameServer::handleMatch(Player* pl)
{
    if (m_waiting.contains(pl)) {
        sendToPlayer(pl, QJsonObject{{"type","waiting"},{"msg","已在匹配队列中"}});
        return;
    }

    if (!m_waiting.isEmpty()) {
        Player* other = m_waiting.dequeue();
        // 为两人创建房间
        Room* r = new Room;
        r->id = QString("room_%1").arg(++m_roomCounter);
        r->p1 = other;
        r->p2 = pl;
        r->p1Ready = false;
        r->p2Ready = false;

        other->roomId = r->id;
        other->isBlack = true;
        other->ready = false;

        pl->roomId = r->id;
        pl->isBlack = false;
        pl->ready = false;

        m_rooms.insert(r->id, r);

        // 构造包含完整玩家信息 (包括胜负场) 的JSON对象
        QJsonObject m1;
        m1["type"] = "matched";
        m1["room_id"] = r->id;
        m1["color"] = "black";
        QJsonObject you1;
        you1["id"] = other->userId;
        you1["username"] = other->username;
        you1["nickname"] = other->nickname;
        you1["rating"] = other->rating;
        you1["wins"] = other->wins;
        you1["losses"] = other->losses;
        m1["you"] = you1;
        QJsonObject opp1;
        opp1["id"] = pl->userId;
        opp1["username"] = pl->username;
        opp1["nickname"] = pl->nickname;
        opp1["rating"] = pl->rating;
        opp1["wins"] = pl->wins;
        opp1["losses"] = pl->losses;
        m1["opponent"] = opp1;

        QJsonObject m2;
        m2["type"] = "matched";
        m2["room_id"] = r->id;
        m2["color"] = "white";
        QJsonObject you2;
        you2["id"] = pl->userId;
        you2["username"] = pl->username;
        you2["nickname"] = pl->nickname;
        you2["rating"] = pl->rating;
        you2["wins"] = pl->wins;
        you2["losses"] = pl->losses;
        m2["you"] = you2;
        QJsonObject opp2;
        opp2["id"] = other->userId;
        opp2["username"] = other->username;
        opp2["nickname"] = other->nickname;
        opp2["rating"] = other->rating;
        opp2["wins"] = other->wins;
        opp2["losses"] = other->losses;
        m2["opponent"] = opp2;

        // 分别发送给匹配成功的双方
        sendToPlayer(other, m1);
        sendToPlayer(pl, m2);

        broadcastRoomList();
        qDebug() << "配对成功, 房间:" << r->id << " 玩家:" << other->id << " vs " << pl->id;
        return;
    } else {
        m_waiting.enqueue(pl);
        sendToPlayer(pl, QJsonObject{{"type","waiting"},{"msg","已进入匹配队列"}} );
    }
}

void GameServer::handleCreateRoom(Player* pl)
{
    Room* r = new Room;
    r->id = QString("room_%1").arg(++m_roomCounter);
    r->p1 = pl;
    r->p2 = nullptr;
    r->p1Ready = false;
    r->p2Ready = false;
    pl->roomId = r->id;
    pl->isBlack = true; // 房主默认执黑
    pl->ready = false;
    m_rooms.insert(r->id, r);

    // 准备JSON消息, 包含"you"和空的"opponent"对象以保持格式一致
    QJsonObject jo;
    jo["type"] = "room_joined";
    jo["room_id"] = r->id;
    jo["color"] = "black";

    QJsonObject you;
    you["id"] = pl->userId;
    you["username"] = pl->username;
    you["nickname"] = pl->nickname;
    you["rating"] = pl->rating;
    jo["you"] = you;
    jo["opponent"] = QJsonObject(); // 空的对手信息

    sendToPlayer(pl, jo);

    qDebug() << "创建房间" << r->id << ", 房主:" << pl->username;
    broadcastRoomList();
}

void GameServer::handleJoinRoom(Player* pl, const QString &roomId)
{
    if (!m_rooms.contains(roomId)) {
        sendToPlayer(pl, QJsonObject{{"type","error"},{"msg","房间不存在"}});
        return;
    }
    Room* r = m_rooms[roomId];
    if (r->p2 != nullptr) {
        sendToPlayer(pl, QJsonObject{{"type","error"},{"msg","房间已满"}});
        return;
    }

    // 将玩家添加为房间的 p2
    r->p2 = pl;
    pl->roomId = roomId;
    pl->isBlack = false;
    pl->ready = false;

    // 向加入者发送 "room_joined" 消息, 触发其进入房间界面
    QJsonObject jo2;
    jo2["type"] = "room_joined";
    jo2["room_id"] = roomId;
    QJsonObject you2;
    you2["id"] = pl->userId;
    you2["username"] = pl->username;
    you2["nickname"] = pl->nickname;
    you2["rating"] = pl->rating;
    jo2["you"] = you2;
    QJsonObject opp2;
    opp2["id"] = r->p1->userId;
    opp2["username"] = r->p1->username;
    opp2["nickname"] = r->p1->nickname;
    opp2["rating"] = r->p1->rating;
    jo2["opponent"] = opp2;
    sendToPlayer(pl, jo2);

    // 向房主发送 "opponent_joined" 消息, 用于更新其界面信息
    QJsonObject jo1;
    jo1["type"] = "opponent_joined"; // 注意: 类型不同, 避免房主重复创建窗口
    jo1["room_id"] = roomId;
    QJsonObject oppInfo;
    oppInfo["id"] = pl->userId;
    oppInfo["username"] = pl->username;
    oppInfo["nickname"] = pl->nickname;
    oppInfo["rating"] = pl->rating;
    jo1["opponent"] = oppInfo;
    QJsonObject hostInfo; // 可选地附上房主自己的信息
    hostInfo["id"] = r->p1->userId;
    hostInfo["username"] = r->p1->username;
    hostInfo["nickname"] = r->p1->nickname;
    hostInfo["rating"] = r->p1->rating;
    jo1["you"] = hostInfo;
    sendToPlayer(r->p1, jo1);

    qDebug() << pl->username << "加入房间" << roomId;

    broadcastRoomList();
}

void GameServer::tryStartWhenReady(Room* room)
{
    if (!room || !room->p1 || !room->p2) return;
    if (room->p1Ready && room->p2Ready) {
        QJsonObject s1{{"type","start"},{"color","black"}};
        QJsonObject s2{{"type","start"},{"color","white"}};
        sendToPlayer(room->p1, s1);
        sendToPlayer(room->p2, s2);
        room->p1Ready = false;
        room->p2Ready = false;
        qDebug() << "房间开始:" << room->id;
    }
}

void GameServer::sendToPlayer(Player* pl, const QJsonObject &obj)
{
    if (!pl || !pl->socket) return;
    QJsonDocument doc(obj);
    pl->socket->sendTextMessage(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

void GameServer::sendToOpponent(Player* pl, const QJsonObject &obj)
{
    if (!pl || pl->roomId.isEmpty() || !m_rooms.contains(pl->roomId)) return;
    Room* r = m_rooms[pl->roomId];
    Player* other = (r->p1 == pl ? r->p2 : r->p1);
    if (other && other->socket) {
        sendToPlayer(other, obj);
    }
}

void GameServer::sendRoomListToPlayer(Player* pl)
{
    QJsonArray arr;
    for (auto it = m_rooms.begin(); it != m_rooms.end(); ++it) {
        Room* r = it.value();
        QJsonObject jo;
        jo["room_id"] = r->id;
        int cnt = 0;
        if (r->p1) ++cnt;
        if (r->p2) ++cnt;
        jo["players"] = cnt;
        jo["status"] = (r->p1 && r->p2) ? QString("游戏中") : QString("等待中");
        // 包含玩家摘要信息
        if (r->p1) {
            QJsonObject p1; p1["nickname"] = r->p1->nickname; p1["username"]=r->p1->username; p1["rating"]=r->p1->rating;
            jo["p1"] = p1;
        }
        if (r->p2) {
            QJsonObject p2; p2["nickname"] = r->p2->nickname; p2["username"]=r->p2->username; p2["rating"]=r->p2->rating;
            jo["p2"] = p2;
        }
        arr.append(jo);
    }
    QJsonObject out; out["type"]="room_list"; out["rooms"]=arr;
    sendToPlayer(pl, out);
}

Room* GameServer::getRoomForPlayer(Player* pl)
{
    if (!pl) return nullptr;
    if (!pl->roomId.isEmpty() && m_rooms.contains(pl->roomId)) return m_rooms[pl->roomId];

    // 后备方案: 遍历查找 (理论上 roomId 应该总是正确的)
    for (auto it = m_rooms.begin(); it != m_rooms.end(); ++it) {
        Room* r = it.value();
        if (!r) continue;
        if (r->p1 == pl || r->p2 == pl) return r;
    }
    return nullptr;
}

void GameServer::broadcastRoomList()
{
    for (auto it = m_map.begin(); it != m_map.end(); ++it) {
        Player* pl = it.value();
        if (pl) sendRoomListToPlayer(pl);
    }
}

void GameServer::onSocketDisconnected()
{
    QWebSocket* sock = qobject_cast<QWebSocket*>(sender());
    if (!sock || !m_map.contains(sock)) return;
    Player* pl = m_map[sock];

    qDebug() << "玩家断开连接:" << pl->id;

    // 如果在等待队列中, 则移除
    m_waiting.removeAll(pl);

    // 如果在房间中, 通知对手并清理房间
    QString rid = pl->roomId;
    if (!rid.isEmpty() && m_rooms.contains(rid)) {
        Room* r = m_rooms[rid];
        Player* other = (r->p1 == pl ? r->p2 : r->p1);
        if (other) {
            sendToPlayer(other, QJsonObject{{"type","opponent_left"},{"room_id", rid}});
            other->roomId.clear();
        }
        m_rooms.remove(rid);
        delete r;
        broadcastRoomList();
    }

    m_map.remove(sock);
    sock->deleteLater();
    delete pl;
}
