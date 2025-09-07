// main.cpp
#include <QApplication>
#include <QJsonObject>
#include "networkmanager.h"
#include "loginwindow.h"
#include "lobbywindow.h"
#include "gamewindow.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 全局共享的网络管理器 (使用 new 创建以确保在各窗口间传递指针时其生命周期稳定)
    NetworkManager *netMgr = new NetworkManager(nullptr);

    // 登录窗口也使用 new 创建, 便于在 lambda 中安全地捕获其指针
    LoginWindow *login = new LoginWindow(netMgr);
    login->show();

    // 当登录成功时, 创建并显示大厅窗口
    QObject::connect(login, &LoginWindow::loginSucceeded, login,
        [netMgr, login](const QJsonObject &userInfo) {
            // 创建大厅窗口
            LobbyWindow *lobby = new LobbyWindow(netMgr, userInfo);
            lobby->show();

            // 隐藏登录窗口 (不立即删除, 以便后续登出时能重新显示)
            login->hide();

            // 当大厅发出 enterRoom 信号时, 创建并显示游戏窗口
            // 注意: 使用 lobby 作为 connect 的上下文对象, 这样当 lobby 被销毁时, 这个连接会自动断开
            QObject::connect(lobby, &LobbyWindow::enterRoom, lobby,
                [netMgr, lobby](const QJsonObject &roomInfo) {
                    // 从房间信息中提取"你"的玩家数据 (QJsonObject 按值拷贝是安全的)
                    QJsonObject youObj = roomInfo.value("you").toObject();

                    // 创建游戏窗口
                    GameWindow *game = new GameWindow(netMgr, youObj, roomInfo);
                    game->show();

                    // 隐藏大厅窗口
                    lobby->hide();

                    // 处理从游戏窗口返回大厅的逻辑
                    QObject::connect(game, &GameWindow::exitToLobby, lobby, [lobby, game]() {
                        // 重置大厅的"在房间内"状态
                        lobby->setInRoom(false);
                        lobby->show();
                        // 使用 deleteLater 安全地删除游戏窗口, 避免在当前事件处理栈中直接析构
                        game->deleteLater();
                    });
                }
            );

            // 处理从大厅登出的逻辑
            QObject::connect(lobby, &LobbyWindow::loggedOut, login, [login, lobby]() {
                login->show();
                // 同样使用 deleteLater 安全地删除大厅窗口
                lobby->deleteLater();
            });
        }
    );

    return a.exec();
}
