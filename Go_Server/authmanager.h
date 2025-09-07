#ifndef AUTHMANAGER_H
#define AUTHMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QJsonObject>

class AuthManager : public QObject
{
    Q_OBJECT
public:
    explicit AuthManager(QObject *parent = nullptr);
    ~AuthManager();

    // 连接数据库 (初始化时调用)
    bool openDatabase(const QString &host, int port,
                      const QString &dbName,
                      const QString &user, const QString &password,
                      QString &errMsg);

    // 注册用户
    QJsonObject registerUser(const QString &username,
                             const QString &password,
                             const QString &nickname);

    // 用户登录
    QJsonObject loginUser(const QString &username,
                          const QString &password);

    // 通过ID获取用户资料
    QJsonObject getUserProfileById(int userId);

    // 更新对局结果 (胜/负)
    bool updateResult(int userId, bool win, QString &errMsg);

private:
    QSqlDatabase m_db;

    // 辅助函数
    // 生成盐值 (十六进制字符串)
    static QString genSaltHex(int len = 8);
    // 计算 SHA256 哈希值
    static QString sha256Hex(const QString &input);
};

#endif // AUTHMANAGER_H
