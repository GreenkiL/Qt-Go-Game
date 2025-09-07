#include "authmanager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QVariant>
#include <QDebug>

AuthManager::AuthManager(QObject *parent) : QObject(parent)
{
}

AuthManager::~AuthManager()
{
    if (m_db.isOpen()) m_db.close();
}

bool AuthManager::openDatabase(const QString &host, int port,
                               const QString &dbName,
                               const QString &user, const QString &password,
                               QString &errMsg)
{
    if (QSqlDatabase::isDriverAvailable("QMYSQL") == false) {
        errMsg = QStringLiteral("MySQL 驱动 (QMYSQL) 不可用, 请确保已安装 Qt 的 MySQL 插件");
        return false;
    }

    m_db = QSqlDatabase::addDatabase("QMYSQL", "AuthConnection");
    m_db.setHostName(host);
    m_db.setPort(port);
    m_db.setDatabaseName(dbName);
    m_db.setUserName(user);
    m_db.setPassword(password);
    m_db.setConnectOptions("MYSQL_OPT_RECONNECT=1");

    if (!m_db.open()) {
        QSqlError e = m_db.lastError();
        qDebug() << "数据库连接失败: " << e.text();
        return false;
    }

    return true;
}

QString AuthManager::genSaltHex(int len)
{
    QByteArray bytes;
    bytes.resize(len);
    for (int i = 0; i < len; ++i) {
        bytes[i] = static_cast<char>(QRandomGenerator::global()->bounded(0, 256));
    }
    return bytes.toHex(); // 返回十六进制字符串
}

QString AuthManager::sha256Hex(const QString &input)
{
    QByteArray hash = QCryptographicHash::hash(input.toUtf8(), QCryptographicHash::Sha256);
    return hash.toHex();
}

QJsonObject AuthManager::registerUser(const QString &username,
                                     const QString &password,
                                     const QString &nickname)
{
    QJsonObject res;
    if (!m_db.isOpen()) {
        res["success"] = false;
        res["msg"] = QStringLiteral("数据库未连接");
        return res;
    }
    if (username.isEmpty() || password.isEmpty()) {
        res["success"] = false;
        res["msg"] = QStringLiteral("用户名和密码不能为空");
        return res;
    }

    // 生成盐值和哈希后的密码
    QString salt = genSaltHex(8);
    QString hashed = sha256Hex(password + salt);

    QSqlQuery q(m_db);
    q.prepare("INSERT INTO users(username, password_hash, salt, nickname) VALUES(:u, :h, :s, :n)");
    q.bindValue(":u", username);
    q.bindValue(":h", hashed);
    q.bindValue(":s", salt);
    q.bindValue(":n", nickname);

    if (!q.exec()) {
        QString err = q.lastError().text();
        if (err.contains("Duplicate") || err.contains("UNIQUE")) {
            res["success"] = false;
            res["msg"] = QStringLiteral("用户名已存在");
        } else {
            res["success"] = false;
            res["msg"] = err;
        }
        return res;
    }

    res["success"] = true;
    res["msg"] = QStringLiteral("注册成功");
    return res;
}

QJsonObject AuthManager::loginUser(const QString &username,
                                  const QString &password)
{
    QJsonObject res;
    if (!m_db.isOpen()) {
        res["success"] = false;
        res["msg"] = QStringLiteral("数据库未连接");
        return res;
    }
    if (username.isEmpty() || password.isEmpty()) {
        res["success"] = false;
        res["msg"] = QStringLiteral("用户名或密码为空");
        return res;
    }

    QSqlQuery q(m_db);
    q.prepare("SELECT id, password_hash, salt, nickname, rating, wins, losses FROM users WHERE username = :u LIMIT 1");
    q.bindValue(":u", username);
    if (!q.exec()) {
        res["success"] = false;
        res["msg"] = q.lastError().text();
        return res;
    }

    if (!q.next()) {
        res["success"] = false;
        res["msg"] = QStringLiteral("用户名不存在");
        return res;
    }

    int id = q.value("id").toInt();
    QString dbHash = q.value("password_hash").toString();
    QString salt = q.value("salt").toString();
    QString nickname = q.value("nickname").toString();
    int rating = q.value("rating").toInt();
    int wins = q.value("wins").toInt();
    int losses = q.value("losses").toInt();

    QString hashed = sha256Hex(password + salt);
    if (hashed != dbHash) {
        res["success"] = false;
        res["msg"] = QStringLiteral("密码错误");
        return res;
    }

    res["success"] = true;
    QJsonObject user;
    user["id"] = id;
    user["username"] = username;
    user["nickname"] = nickname;
    user["rating"] = rating;
    user["wins"] = wins;
    user["losses"] = losses;
    res["user"] = user;
    return res;
}

QJsonObject AuthManager::getUserProfileById(int userId)
{
    QJsonObject res;
    if (!m_db.isOpen()) {
        res["success"] = false;
        res["msg"] = QStringLiteral("数据库未连接");
        return res;
    }
    QSqlQuery q(m_db);
    q.prepare("SELECT id, username, nickname, rating, wins, losses FROM users WHERE id = :id LIMIT 1");
    q.bindValue(":id", userId);
    if (!q.exec()) {
        res["success"] = false;
        res["msg"] = q.lastError().text();
        return res;
    }
    if (!q.next()) {
        res["success"] = false;
        res["msg"] = QStringLiteral("用户不存在");
        return res;
    }
    res["success"] = true;
    QJsonObject user;
    user["id"] = q.value("id").toInt();
    user["username"] = q.value("username").toString();
    user["nickname"] = q.value("nickname").toString();
    user["rating"] = q.value("rating").toInt();
    user["wins"] = q.value("wins").toInt();
    user["losses"] = q.value("losses").toInt();
    res["user"] = user;
    return res;
}

bool AuthManager::updateResult(int userId, bool win, QString &errMsg)
{
    if (!m_db.isOpen()) {
        errMsg = QStringLiteral("数据库未连接");
        return false;
    }
    // 采用非常简单的积分更新: 赢+10, 输-10
    QSqlQuery q(m_db);
    if (!q.exec("START TRANSACTION")) {
        errMsg = q.lastError().text();
        return false;
    }
    if (win) {
        q.prepare("UPDATE users SET wins = wins + 1, rating = rating + 10 WHERE id = :id");
    } else {
        q.prepare("UPDATE users SET losses = losses + 1, rating = rating - 10 WHERE id = :id");
    }
    q.bindValue(":id", userId);
    if (!q.exec()) {
        q.exec("ROLLBACK");
        errMsg = q.lastError().text();
        return false;
    }
    if (!q.exec("COMMIT")) {
        errMsg = q.lastError().text();
        return false;
    }
    return true;
}
