#ifndef BOARDWIDGET_H
#define BOARDWIDGET_H

#include <QWidget>
#include <QPoint>
#include <QVector>
#include "goban.h"

class NetworkManager;

class BoardWidget : public QWidget
{
    Q_OBJECT
public:
    explicit BoardWidget(QWidget *parent = nullptr);

    QSize minimumSizeHint() const override { return QSize(600,600); }
    QSize sizeHint() const override { return QSize(700,700); }

    void playerPass();
    void newGame();
    void setAIEnabled(bool enabled);
    int currentPlayer() const;

    // 计算中国规则下的得分: <黑棋得分, 白棋得分>
    QPair<int,int> computeChineseScore();

    // 提供对内部 Goban 对象的只读访问
    const Goban& goban() const;

    // 网络
    void setNetworkManager(NetworkManager *mgr);
    void setNetworkModeEnabled(bool enabled);
    bool isNetworkMode() const;

    // 本地玩家颜色管理
    void setLocalPlayerColor(int color);
    int localPlayerColor() const;

    // 用于同步的棋盘序列化
    QString serializeBoard() const;
    bool loadBoardFromSerialized(const QString &ser, int currentPlayer);

    // 网络/本地落子辅助
    void applyRemoteMove(int i, int j);
    void playLocalMove(int i, int j);

    // AI行为
    void doAIMove(int difficulty);

public slots:
    void displayAnalysis(const QVector<double>& ownershipMap);
    void clearAnalysis();

signals:
    // 通知主窗口更新状态标签
    void stateChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    Goban m_board;
    int m_viewMargin;
    int m_gridSize;
    bool m_aiEnabled;

    // 网络相关
    NetworkManager *m_net = nullptr;
    bool m_networkMode = false;
    int m_localColor = 0; // 0 = 未设置, 1 = 黑棋, 2 = 白棋
    QVector<double> m_ownershipMap;

    void tryPlay(int i, int j, bool sendNetwork=true);
    void maybeAIMove();
};

#endif // BOARDWIDGET_H
