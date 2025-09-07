#include "lobbywindow.h"
#include "networkmanager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QMessageBox>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <QTimer>
#include <QJsonDocument>
#include <QPointer>

LobbyWindow::LobbyWindow(NetworkManager *netMgr, const QJsonObject &userInfo, QWidget *parent)
    : QWidget(parent), m_net(netMgr), m_user(userInfo)
{
    QVBoxLayout *main = new QVBoxLayout(this);

    QLabel *title = new QLabel(tr("<h2>游戏大厅</h2>"), this);
    main->addWidget(title);

    // 显示玩家个人信息
    m_profileLabel = new QLabel(this);
    QString prof = tr("昵称: %1\n段位: %2\n胜: %3  负: %4")
            .arg(m_user.value("nickname").toString().isEmpty() ? m_user.value("username").toString() : m_user.value("nickname").toString())
            .arg(m_user.value("rating").toInt())
            .arg(m_user.value("wins").toInt())
            .arg(m_user.value("losses").toInt());
    m_profileLabel->setText(prof);
    main->addWidget(m_profileLabel);

    m_roomList = new QListWidget(this);
    main->addWidget(m_roomList, 1);

    // 功能按钮
    QHBoxLayout *btns = new QHBoxLayout();
    m_createBtn = new QPushButton(tr("创建房间"), this);
    m_joinBtn = new QPushButton(tr("加入房间"), this);
    m_matchBtn = new QPushButton(tr("匹配"), this);
    m_cancelMatchBtn = new QPushButton(tr("取消匹配"), this);
    m_cancelMatchBtn->setEnabled(false);
    m_refreshBtn = new QPushButton(tr("刷新列表"), this);
    btns->addWidget(m_createBtn);
    btns->addWidget(m_joinBtn);
    btns->addWidget(m_matchBtn);
    btns->addWidget(m_cancelMatchBtn);
    btns->addWidget(m_refreshBtn);
    main->addLayout(btns);

    m_status = new QLabel(tr("就绪"), this);
    main->addWidget(m_status);

    m_logoutBtn = new QPushButton(tr("登出"), this);
    main->addWidget(m_logoutBtn);

    m_singleBtn = new QPushButton(tr("单机对战"), this);
    btns->addWidget(m_singleBtn);

    connect(m_createBtn, &QPushButton::clicked, this, &LobbyWindow::onCreateRoom);
    connect(m_joinBtn, &QPushButton::clicked, this, &LobbyWindow::onJoinRoom);
    connect(m_matchBtn, &QPushButton::clicked, this, &LobbyWindow::onMatch);
    connect(m_cancelMatchBtn, &QPushButton::clicked, this, &LobbyWindow::onCancelMatch);
    connect(m_logoutBtn, &QPushButton::clicked, this, &LobbyWindow::onLogout);
    connect(m_refreshBtn, &QPushButton::clicked, this, &LobbyWindow::onRefreshRooms);
    connect(m_singleBtn, &QPushButton::clicked, this, &LobbyWindow::onSinglePlayer);

    connect(m_net, &NetworkManager::jsonReceived, this, &LobbyWindow::onNetworkJsonReceived, Qt::QueuedConnection);
    connect(m_net, &NetworkManager::logMessage, this, &LobbyWindow::onLogMessage, Qt::QueuedConnection);


    // 进入大厅后自动刷新一次房间列表
    if (m_net->isConnected()) {
        onRefreshRooms();
    } else {
        // 如果未连接, 尝试重连 (正常情况下 LoginWindow 已连接)
        m_net->connectToHost(QUrl("ws://127.0.0.1:12345"));
    }
}

void LobbyWindow::onCreateRoom()
{
    if (!m_net->isConnected()) { QMessageBox::warning(this, tr("未连接"), tr("请先连接服务器")); return; }
    QJsonObject obj; obj["type"] = "create_room";
    m_net->sendJson(obj);
    m_status->setText(tr("已请求创建房间"));
}

void LobbyWindow::onJoinRoom()
{
    if (!m_net->isConnected()) { QMessageBox::warning(this, tr("未连接"), tr("请先连接服务器")); return; }
    bool ok = false;
    QString rid = QInputDialog::getText(this, tr("加入房间"), tr("请输入房间 ID:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || rid.isEmpty()) return;
    QJsonObject obj; obj["type"] = "join_room"; obj["room_id"] = rid;
    m_net->sendJson(obj);
    m_status->setText(tr("尝试加入房间 %1").arg(rid));
}

void LobbyWindow::onMatch()
{
    if (!m_net->isConnected()) { QMessageBox::warning(this, tr("未连接"), tr("请先连接服务器")); return; }
    QJsonObject obj;
    obj["type"] = "match";
    m_net->sendJson(obj);
    m_status->setText(tr("正在排队匹配..."));
    m_matchBtn->setEnabled(false);
    m_cancelMatchBtn->setEnabled(true);
}

void LobbyWindow::onCancelMatch()
{
    if (!m_net->isConnected()) { QMessageBox::warning(this, tr("未连接"), tr("请先连接服务器")); return; }
    QJsonObject obj;
    obj["type"] = "cancel_match";
    m_net->sendJson(obj);
    m_status->setText(tr("已发出取消匹配请求"));
    m_matchBtn->setEnabled(true);
    m_cancelMatchBtn->setEnabled(false);
}

void LobbyWindow::onLogout()
{
    if (m_net->isConnected()) {
        QJsonObject obj; obj["type"]="logout";
        m_net->sendJson(obj);
    }
    emit loggedOut();
}

void LobbyWindow::onRefreshRooms()
{
    if (!m_net->isConnected()) { QMessageBox::warning(this, tr("未连接"), tr("请先连接服务器")); return; }
    QJsonObject obj; obj["type"]="list_rooms";
    m_net->sendJson(obj);
    m_status->setText(tr("请求房间列表..."));
}

void LobbyWindow::onNetworkJsonReceived(const QJsonObject &obj)
{
    QString t = obj.value("type").toString();

    if (t == "room_list") {
        // 更新房间列表
        m_roomList->clear();
        QJsonArray arr = obj.value("rooms").toArray();
        for (auto v : arr) {
            QJsonObject r = v.toObject();
            QString rid = r.value("room_id").toString();
            int players = r.value("players").toInt();
            QString status = r.value("status").toString();
            QString text = tr("房间 %1  [%2人] 状态:%3").arg(rid).arg(players).arg(status);
            if (r.contains("p1")) text += "\n  " + r.value("p1").toObject().value("nickname").toString();
            if (r.contains("p2")) text += "\n  " + r.value("p2").toObject().value("nickname").toString();
            QListWidgetItem *it = new QListWidgetItem(text, m_roomList);
            it->setData(Qt::UserRole, rid);
        }
        m_status->setText(tr("房间列表已更新"));
        return;
    }

    if (t == "match_cancelled") {
        m_status->setText(obj.value("msg").toString());
        m_matchBtn->setEnabled(true);
        m_cancelMatchBtn->setEnabled(false);
        return;
    }

    // 收到 "room_joined" 消息, 准备进入房间
    if (t == "room_joined") {
        // 如果已在房间内, 忽略重复的消息 (幂等保护)
        if (m_inRoom) {
            qDebug() << "[Lobby] already in room, ignoring duplicate room_joined";
            return;
        }

        qDebug() << "[Lobby] recv room_joined:" << QJsonDocument(obj).toJson(QJsonDocument::Compact);

        QString rid = obj.value("room_id").toString();

        // 安全地提取 "you" 和 "opponent" 对象
        QJsonValue youVal = obj.value("you");
        QJsonObject you = youVal.isObject() ? youVal.toObject() : QJsonObject();
        QJsonValue oppVal = obj.value("opponent");
        QJsonObject opp = oppVal.isObject() ? oppVal.toObject() : QJsonObject();

        // 如果服务器消息中 "you" 为空, 使用本地用户信息作为后备
        if (you.isEmpty()) {
            QJsonObject youFallback;
            youFallback["id"] = m_user.value("id").toInt();
            youFallback["username"] = m_user.value("username").toString();
            youFallback["nickname"] = m_user.value("nickname").toString();
            youFallback["rating"] = m_user.value("rating").toInt();
            you = youFallback;
        }

        // 构造进入房间所需的信息
        QJsonObject roominfo;
        roominfo["room_id"] = rid;
        roominfo["you"] = you;
        roominfo["opponent"] = opp;
        if (obj.contains("color")) roominfo["color"] = obj.value("color");

        m_inRoom = true; // 标记为已进入房间, 避免重复进入
        m_matchBtn->setEnabled(true);
        m_cancelMatchBtn->setEnabled(false);

        qDebug() << "[Lobby] entering room, roominfo:" << QJsonDocument(roominfo).toJson(QJsonDocument::Compact);

        emit enterRoom(roominfo); // 发出信号, 通知主窗口切换到游戏界面
        return;
    }

    // "matched" 消息也视为进入房间
    if (t == "matched") {
        if (m_inRoom) {
            qDebug() << "[Lobby] already in room, ignoring duplicate matched";
            return;
        }

        qDebug() << "[Lobby] recv matched:" << QJsonDocument(obj).toJson(QJsonDocument::Compact);

        QJsonValue youVal = obj.value("you");
        QJsonObject you = youVal.isObject() ? youVal.toObject() : QJsonObject();
        QJsonValue oppVal = obj.value("opponent");
        QJsonObject opp = oppVal.isObject() ? oppVal.toObject() : QJsonObject();
        QString rid = obj.value("room_id").toString();
        QString color = obj.value("color").toString();

        if (you.isEmpty()) {
            QJsonObject youFallback;
            youFallback["id"] = m_user.value("id").toInt();
            youFallback["username"] = m_user.value("username").toString();
            youFallback["nickname"] = m_user.value("nickname").toString();
            youFallback["rating"] = m_user.value("rating").toInt();
            you = youFallback;
        }

        QJsonObject roominfo;
        roominfo["room_id"] = rid;
        roominfo["you"] = you;
        roominfo["opponent"] = opp;
        roominfo["color"] = color;

        m_inRoom = true;
        qDebug() << "[Lobby] entering matched room, roominfo:" << QJsonDocument(roominfo).toJson(QJsonDocument::Compact);

        m_matchBtn->setEnabled(true);
        m_cancelMatchBtn->setEnabled(false);
        emit enterRoom(roominfo);
        return;
    }

    if (t == "waiting") {
        m_status->setText(obj.value("msg").toString());
        return;
    }

    if (t == "start") {
        m_status->setText(tr("房间开始对局"));
        return;
    }

    if (t == "error") {
        QString msg = obj.value("msg").toString();
        QMessageBox::warning(this, tr("错误"), msg);
        m_status->setText(tr("错误: %1").arg(msg));
        return;
    }

    // 大厅忽略其他类型的消息
}


void LobbyWindow::onLogMessage(const QString &msg)
{
    m_status->setText(msg);
}

void LobbyWindow::setInRoom(bool inRoom)
{
    m_inRoom = inRoom;
    // 离开房间时, 恢复UI状态
    if (!m_inRoom) {
        m_matchBtn->setEnabled(true);
        m_cancelMatchBtn->setEnabled(false);
        m_status->setText(tr("已回到大厅"));
    }
}

void LobbyWindow::onSinglePlayer()
{
    // 弹窗让玩家选择执子颜色
    QStringList colors = { tr("执黑"), tr("执白") };
    bool okColor = false;
    QString colorChoice = QInputDialog::getItem(this, tr("选择棋色"), tr("请选择你的棋子颜色:"), colors, 0, false, &okColor);
    if (!okColor) return;
    QString color = (colorChoice == tr("执黑")) ? "black" : "white";

    // 弹窗让玩家选择AI难度
    QStringList levels = { tr("初级 (随机)"), tr("中级 (传统算法)"), tr("高级 (深度学习)") };
    bool okLevel = false;
    QString levelChoice = QInputDialog::getItem(this, tr("选择难度"), tr("请选择AI难度:"), levels, 0, false, &okLevel);
    if (!okLevel) return;
    int aiLevel = levels.indexOf(levelChoice);

    // 构造房间信息, 标记为单机模式
    QJsonObject roominfo;
    roominfo["room_id"] = QString("single_%1").arg(QDateTime::currentMSecsSinceEpoch());
    roominfo["singleplayer"] = true;
    roominfo["color"] = color;
    roominfo["ai_level"] = aiLevel;
    roominfo["you"] = m_user;
    roominfo["opponent"] = QJsonObject(); // 单机模式没有对手信息

    qDebug() << "[Lobby] entering singleplayer room, roominfo:" << QJsonDocument(roominfo).toJson(QJsonDocument::Compact);

    // 直接进入房间
    m_inRoom = true;
    emit enterRoom(roominfo);
}
