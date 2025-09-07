#ifndef SINGLEPLAYER_H
#define SINGLEPLAYER_H

#include <QObject>
#include <QPair>
#include <QProcess>
#include <QByteArray>
#include <QJsonObject>

class BoardWidget;
class QTimer;

/*
 * SinglePlayerManager
 *  - 管理单机模式的 AI
 *  - 头文件中只声明槽/信号, 实现代码位于 singleplayer.cpp
 */
class SinglePlayerManager : public QObject
{
    Q_OBJECT
public:
    explicit SinglePlayerManager(QObject *parent = nullptr);
    ~SinglePlayerManager();

    // 绑定棋盘 (BoardWidget)
    void attachBoard(BoardWidget *board);

    // 启动单机模式; aiColor: 1=黑, 2=白; level: 0/1/2...
    void start(int aiColor, int level = 0);
    // 停止单机模式
    void stop();
    bool isRunning() const { return m_running; }
    // 请求形势判断
    void requestAnalysis();

signals:
    // AI 计算出下一步后发出此信号 (坐标为 (-1,-1) 表示虚着)
    void moveReady(int i, int j);
    // 形势判断数据准备就绪
    void analysisReady(const QJsonObject& analysisData);

private slots:
    void onBoardStateChanged();
    void onTimerTimeout();

    // KataGo 进程有输出时触发
    void onKataGoReadyRead();
    // KataGo 进程出错时触发
    void onKataGoError(QProcess::ProcessError error);

private:
    // 等级0和1的AI走棋逻辑
    QPair<int,int> chooseMoveLvl0_1();
    // 向 KataGo 引擎请求下一步走棋
    void requestKataGoMove();

    BoardWidget *m_board;
    int m_aiColor;
    int m_aiLevel;
    bool m_running;
    QTimer *m_timer;

    QProcess *m_kataGoProcess = nullptr;
    QByteArray m_kataGoBuffer;
};

#endif // SINGLEPLAYER_H
