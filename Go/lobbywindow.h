#ifndef LOBBYWINDOW_H
#define LOBBYWINDOW_H

#include <QWidget>
#include <QJsonObject>

class NetworkManager;
class QLabel;
class QListWidget;
class QPushButton;

class LobbyWindow : public QWidget
{
    Q_OBJECT
public:
    explicit LobbyWindow(NetworkManager *netMgr, const QJsonObject &userInfo, QWidget *parent = nullptr);

signals:
    // 进入房间信号, 携带房间信息
    void enterRoom(const QJsonObject &roomInfo);
    // 登出信号
    void loggedOut();

public slots:
    // 设置是否在房间内的状态
    void setInRoom(bool inRoom);

private slots:
    void onCreateRoom();
    void onJoinRoom();
    void onMatch();
    void onCancelMatch();
    void onLogout();
    void onRefreshRooms();
    void onNetworkJsonReceived(const QJsonObject &obj);
    void onLogMessage(const QString &msg);
    void onSinglePlayer();

private:
    NetworkManager *m_net;
    QJsonObject m_user;
    QLabel *m_profileLabel;
    QListWidget *m_roomList;
    QPushButton *m_createBtn;
    QPushButton *m_joinBtn;
    QPushButton *m_matchBtn;
    QPushButton *m_cancelMatchBtn;
    QPushButton *m_logoutBtn;
    QPushButton *m_refreshBtn;
    QPushButton *m_singleBtn = nullptr;
    QLabel *m_status;
    bool m_inRoom = false;
};

#endif // LOBBYWINDOW_H
