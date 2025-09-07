#include "loginwindow.h"
#include "networkmanager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QUrl>

LoginWindow::LoginWindow(NetworkManager *netMgr, QWidget *parent)
    : QWidget(parent), m_net(netMgr)
{
    QVBoxLayout *l = new QVBoxLayout(this);

    QLabel *title = new QLabel(tr("<h2>登录 / 注册</h2>"), this);
    l->addWidget(title);

    m_userEdit = new QLineEdit(this);
    m_userEdit->setPlaceholderText(tr("用户名"));
    l->addWidget(m_userEdit);

    m_passEdit = new QLineEdit(this);
    m_passEdit->setPlaceholderText(tr("密码"));
    m_passEdit->setEchoMode(QLineEdit::Password);
    l->addWidget(m_passEdit);

    m_nickEdit = new QLineEdit(this);
    m_nickEdit->setPlaceholderText(tr("昵称 (注册用)"));
    l->addWidget(m_nickEdit);

    QHBoxLayout *btns = new QHBoxLayout();
    m_registerBtn = new QPushButton(tr("注册"), this);
    m_loginBtn = new QPushButton(tr("登录"), this);
    btns->addWidget(m_registerBtn);
    btns->addWidget(m_loginBtn);
    l->addLayout(btns);

    m_statusLabel = new QLabel(tr("未连接到服务器"), this);
    l->addWidget(m_statusLabel);

    connect(m_registerBtn, &QPushButton::clicked, this, &LoginWindow::onRegisterClicked);
    connect(m_loginBtn, &QPushButton::clicked, this, &LoginWindow::onLoginClicked);

    // 连接网络信号
    connect(m_net, &NetworkManager::connected, this, &LoginWindow::onConnectStateChanged);
    connect(m_net, &NetworkManager::disconnected, this, &LoginWindow::onConnectStateChanged);
    connect(m_net, &NetworkManager::jsonReceived, this, &LoginWindow::onNetworkJsonReceived);
    connect(m_net, &NetworkManager::logMessage, this, &LoginWindow::onLogMessage);

    // 尝试立即连接到固定的服务器地址
    QUrl url(QStringLiteral("ws://%1:%2").arg(SERVER_IP).arg(SERVER_PORT));
    m_net->connectToHost(url);
    m_statusLabel->setText(tr("正在连接服务器..."));
}

void LoginWindow::onConnectStateChanged()
{
    // 此槽函数在连接成功或断开时被调用
    m_statusLabel->setText(tr("已连接服务器"));
}

void LoginWindow::onRegisterClicked()
{
    if (!m_net) return;
    if (!m_net->isConnected()) {
        QMessageBox::warning(this, tr("未连接"), tr("尚未连接服务器"));
        return;
    }
    QString u = m_userEdit->text().trimmed();
    QString p = m_passEdit->text();
    QString n = m_nickEdit->text().trimmed();
    if (u.isEmpty() || p.isEmpty()) {
        QMessageBox::warning(this, tr("输入错误"), tr("用户名/密码不能为空"));
        return;
    }
    QJsonObject obj;
    obj["type"] = "register";
    obj["username"] = u;
    obj["password"] = p;
    obj["nickname"] = n;
    m_net->sendJson(obj);
    m_statusLabel->setText(tr("正在注册..."));
}

void LoginWindow::onLoginClicked()
{
    if (!m_net) return;
    if (!m_net->isConnected()) {
        QMessageBox::warning(this, tr("未连接"), tr("尚未连接服务器"));
        return;
    }
    QString u = m_userEdit->text().trimmed();
    QString p = m_passEdit->text();
    if (u.isEmpty() || p.isEmpty()) {
        QMessageBox::warning(this, tr("输入错误"), tr("用户名/密码不能为空"));
        return;
    }
    QJsonObject obj;
    obj["type"] = "login";
    obj["username"] = u;
    obj["password"] = p;
    m_net->sendJson(obj);
    m_statusLabel->setText(tr("正在登录..."));
}

void LoginWindow::onNetworkJsonReceived(const QJsonObject &obj)
{
    QString t = obj.value("type").toString();
    if (t == "register_result") {
        bool ok = obj.value("success").toBool();
        QString msg = obj.value("msg").toString();
        if (ok) QMessageBox::information(this, tr("注册成功"), msg);
        else QMessageBox::warning(this, tr("注册失败"), msg);
        m_statusLabel->setText(tr("等待操作"));
        return;
    }
    if (t == "login_result") {
        bool ok = obj.value("success").toBool();
        if (!ok) {
            QMessageBox::warning(this, tr("登录失败"), obj.value("msg").toString());
            m_statusLabel->setText(tr("登录失败"));
            return;
        }
        QJsonObject user = obj.value("user").toObject();
        // 发出登录成功信号, 并附带用户信息 (主窗口将用此信息打开大厅)
        emit loginSucceeded(user);
        m_statusLabel->setText(tr("登录成功"));
    }
}

void LoginWindow::onLogMessage(const QString &msg)
{
    m_statusLabel->setText(msg);
}
