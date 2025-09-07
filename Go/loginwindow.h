#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include <QWidget>
#include <QJsonObject>

class NetworkManager;
class QLineEdit;
class QPushButton;
class QLabel;

class LoginWindow : public QWidget
{
    Q_OBJECT
public:
    explicit LoginWindow(NetworkManager *netMgr, QWidget *parent = nullptr);

signals:
    // 登录成功信号, 携带用户信息
    void loginSucceeded(const QJsonObject &userInfo);

private slots:
    void onConnectStateChanged();
    void onRegisterClicked();
    void onLoginClicked();
    void onNetworkJsonReceived(const QJsonObject &obj);
    void onLogMessage(const QString &msg);

private:
    NetworkManager *m_net;
    QLineEdit *m_userEdit;
    QLineEdit *m_passEdit;
    QLineEdit *m_nickEdit;
    QPushButton *m_registerBtn;
    QPushButton *m_loginBtn;
    QLabel *m_statusLabel;

    // 服务器地址 (可按需修改)
    const QString SERVER_IP = QStringLiteral("127.0.0.1");
    const quint16 SERVER_PORT = 12345;
};

#endif // LOGINWINDOW_H
