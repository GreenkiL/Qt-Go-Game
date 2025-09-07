#include "boardwidget.h"
#include "ai_random.h"
#include "networkmanager.h"

#include <QPainter>
#include <QMouseEvent>
#include <QMessageBox>
#include <QTimer>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDebug>
#include <QPointer>

BoardWidget::BoardWidget(QWidget *parent)
    : QWidget(parent),
      m_board(19),
      m_viewMargin(20),
      m_gridSize(0),
      m_aiEnabled(false),
      m_net(nullptr),
      m_networkMode(false),
      m_localColor(0)
{
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Base);
}

void BoardWidget::setNetworkManager(NetworkManager *mgr)
{
    if (m_net == mgr) return;

    // 断开旧的网络连接, 避免重复
    if (m_net) {
        disconnect(m_net, nullptr, this, nullptr);
    }
    m_net = mgr;
    if (!m_net) return;

    // 在lambda表达式中使用 QPointer 作为安全保护, 防止 BoardWidget 对象被删除后出现悬挂指针
    QPointer<BoardWidget> guard(this);

    connect(m_net, &NetworkManager::jsonReceived, this,
        [guard](const QJsonObject &obj) {
            if (!guard) return;
            BoardWidget *self = guard.data();

            QString t = obj.value("type").toString();

            if (t == "move" || (t == "game_update" && obj.value("subtype").toString() == "move")) {
                int i = -1, j = -1;
                if (obj.contains("i") && obj.contains("j")) {
                    i = obj.value("i").toInt();
                    j = obj.value("j").toInt();
                } else if (obj.contains("x") && obj.contains("y")) {
                    i = obj.value("x").toInt();
                    j = obj.value("y").toInt();
                }
                if (i >= 0 && j >= 0) {
                    self->applyRemoteMove(i, j);
                }
            } else if (t == "pass" || (t == "game_update" && obj.value("subtype").toString() == "pass")) {
                self->m_board.pass();
                emit self->stateChanged();
                self->update();
            } else if (t == "turn") {
                int cur = obj.value("currentPlayer").toInt();
                self->m_board.setCurrentPlayer(cur);
                emit self->stateChanged();
                self->update();
            } else if (t == "start") {
                QString color = obj.value("color").toString().toLower();
                if (color == "black") self->setLocalPlayerColor(1);
                else self->setLocalPlayerColor(2);
                self->newGame();
                self->setNetworkModeEnabled(true);
                emit self->stateChanged();
                self->update();
            } else if (t == "resign" || (t == "game_update" && obj.value("subtype").toString() == "resign")) {
                QMessageBox::information(self, QObject::tr("消息"), QObject::tr("对方认输，你获胜"));
                self->setNetworkModeEnabled(false);
                emit self->stateChanged();
                self->update();
            } else if (t == "matched") {
                QString color = obj.value("color").toString().toLower();
                if (color == "black") self->setLocalPlayerColor(1);
                else self->setLocalPlayerColor(2);
                self->setNetworkModeEnabled(false);
                emit self->stateChanged();
                self->update();
            } else if (t == "sync") {
                QString boardStr = obj.value("board").toString();
                int cur = obj.value("currentPlayer").toInt();
                bool ok = self->loadBoardFromSerialized(boardStr, cur);
                if (ok) {
                    emit self->stateChanged();
                    self->update();
                }
            }
            // 在 BoardWidget 层面忽略 "opponent_joined" 消息
        },
        Qt::QueuedConnection
    );
}


void BoardWidget::setNetworkModeEnabled(bool enabled)
{
    m_networkMode = enabled;
    if (m_networkMode) m_aiEnabled = false;
}

bool BoardWidget::isNetworkMode() const { return m_networkMode; }

void BoardWidget::setLocalPlayerColor(int color)
{
    if (color == 1 || color == 2) m_localColor = color;
    else m_localColor = 0;
}

int BoardWidget::localPlayerColor() const { return m_localColor; }

QString BoardWidget::serializeBoard() const
{
    return QString::fromStdString(m_board.serialize());
}

bool BoardWidget::loadBoardFromSerialized(const QString &ser, int currentPlayer)
{
    std::string s = ser.toStdString();
    bool ok = m_board.deserialize(s);
    if (!ok) return false;
    if (currentPlayer == 1 || currentPlayer == 2) m_board.setCurrentPlayer(currentPlayer);
    return true;
}

void BoardWidget::newGame()
{
    m_board.reset();
    clearAnalysis();
    emit stateChanged();
    update();
}

void BoardWidget::setAIEnabled(bool enabled)
{
    m_aiEnabled = enabled;
    maybeAIMove();
}

int BoardWidget::currentPlayer() const
{
    return m_board.currentPlayer();
}

void BoardWidget::playerPass()
{
    if (m_networkMode) {
        if (m_localColor == 0) return;
        if (m_board.currentPlayer() != m_localColor) return;
    }
    m_board.pass();

    if (m_networkMode && m_net && m_net->isConnected()) {
        QJsonObject obj;
        obj["type"] = "pass";
        m_net->sendJson(obj);

        // 发送当前回合给对方
        QJsonObject turn;
        turn["type"] = "turn";
        turn["currentPlayer"] = m_board.currentPlayer();
        m_net->sendJson(turn);
    }

    emit stateChanged();
    update();
    maybeAIMove();
}

void BoardWidget::displayAnalysis(const QVector<double> &ownershipMap)
{
    m_ownershipMap = ownershipMap;
    update(); // 触发重绘
}

void BoardWidget::clearAnalysis()
{
    if (!m_ownershipMap.isEmpty()) {
        m_ownershipMap.clear();
        update(); // 触发重绘
    }
}

void BoardWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    int n = m_board.size();
    int w = width(), h = height();
    int avail = qMin(w, h) - 2*m_viewMargin;
    if (avail < 0) avail = 0;
    m_gridSize = (n > 1) ? (avail / (n - 1)) : 0;
    int boardSize = m_gridSize * (n - 1);
    int left = (w - boardSize) / 2;
    int top = (h - boardSize) / 2;

    // 绘制棋盘背景
    p.fillRect(rect(), QColor(238, 207, 161));

    // 绘制网格线
    p.setPen(Qt::black);
    for (int i = 0; i < n; ++i) {
        p.drawLine(left, top + i*m_gridSize, left + boardSize, top + i*m_gridSize);
        p.drawLine(left + i*m_gridSize, top, left + i*m_gridSize, top + boardSize);
    }

    if (!m_ownershipMap.isEmpty()) {
        p.setPen(Qt::NoPen);
        int n = m_board.size();
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                double owner = m_ownershipMap[i * n + j];
                QPoint center(left + j*m_gridSize, top + i*m_gridSize);
                QRect r(center.x() - m_gridSize/2, center.y() - m_gridSize/2, m_gridSize, m_gridSize);

                if (owner > 0.75) { // 强黑区域
                    p.setBrush(QColor(0, 0, 255, 70)); // 蓝色半透明
                    p.drawRect(r);
                } else if (owner < -0.75) { // 强白区域
                    p.setBrush(QColor(255, 0, 0, 70)); // 红色半透明
                    p.drawRect(r);
                }
            }
        }
    }

    // 绘制19路棋盘的星位
    QVector<QPoint> stars;
    if (n == 19) {
        int coords[] = {3, 9, 15};
        for (int a : coords)
            for (int b : coords)
                stars.append(QPoint(left + a*m_gridSize, top + b*m_gridSize));
    }
    p.setBrush(Qt::black);
    for (auto &pt : stars) {
        p.drawEllipse(pt, 4, 4);
    }

    // 绘制棋子
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            int v = m_board.get(i, j);
            if (v == 0) continue;
            QPoint center(left + j*m_gridSize, top + i*m_gridSize);
            bool isDead = false;
            if (!m_ownershipMap.isEmpty())
            {
              double owner = m_ownershipMap[i * n + j];
              // 如果是黑棋，但所有权判定为强白，则标记为死棋
              if (v == 1 && owner < -0.85) isDead = true;
              // 如果是白棋，但所有权判定为强黑，则标记为死棋
              if (v == 2 && owner > 0.85) isDead = true;
            }
            p.setPen(Qt::black);
            if (v == 1) { // 黑棋
                p.setBrush(Qt::black);
                p.drawEllipse(center, m_gridSize/2 - 2, m_gridSize/2 - 2);
            } else { // 白棋
                p.setBrush(Qt::white);
                p.drawEllipse(center, m_gridSize/2 - 2, m_gridSize/2 - 2);
                p.setPen(Qt::gray);
                p.drawEllipse(center, m_gridSize/2 - 2, m_gridSize/2 - 2);
            }
        }
    }
}

void BoardWidget::mouseReleaseEvent(QMouseEvent *event)
{
    clearAnalysis();
    int n = m_board.size();
    int w = width(), h = height();

    // 根据当前窗口几何尺寸计算局部网格大小 (避免直接使用 m_gridSize)
    int avail = qMin(w, h) - 2*m_viewMargin;
    if (avail < 0) avail = 0;
    int gridSizeLocal = (n > 1) ? (avail / (n - 1)) : 0;
    if (gridSizeLocal <= 0) {
        // 若布局尚未就绪则忽略点击 (防止除零和索引越界)
        return;
    }
    int boardSize = gridSizeLocal * (n - 1);
    int left = (w - boardSize) / 2;
    int top = (h - boardSize) / 2;

    int x = event->x();
    int y = event->y();

    int j = qRound(double(x - left) / gridSizeLocal);
    int i = qRound(double(y - top) / gridSizeLocal);

    if (i < 0 || i >= n || j < 0 || j >= n) return;

    if (m_localColor != 0 && m_board.currentPlayer() != m_localColor) {
        // 不是本地玩家的回合，忽略点击
        return;
    }

    tryPlay(i, j, true);
}

void BoardWidget::tryPlay(int i, int j, bool sendNetwork)
{
    QString err;
    bool ok = m_board.play(i, j, &err);
    if (!ok) {
        QMessageBox::warning(this, tr("非法落子"), err);
        return;
    }

    // 仅当是本地玩家落子且允许时, 才将数据发送到服务器
    if (sendNetwork && m_networkMode && m_net && m_net->isConnected()) {
        QJsonObject obj;
        obj["type"] = "move";
        obj["x"] = i;
        obj["y"] = j;
        m_net->sendJson(obj);

        // 同时发送回合信息以同步
        QJsonObject turn;
        turn["type"] = "turn";
        turn["currentPlayer"] = m_board.currentPlayer();
        m_net->sendJson(turn);
    }

    emit stateChanged();
    update();
    maybeAIMove();
}

void BoardWidget::applyRemoteMove(int i, int j)
{
    QString err;
    bool ok = m_board.play(i, j, &err);
    if (!ok) {
        // 远端落子失败，提示并尝试请求同步（当前简化为仅提示）
        QMessageBox::warning(this, tr("远端落子失败"), err);
        return;
    }
    emit stateChanged();
    update();
}

void BoardWidget::playLocalMove(int i, int j)
{
    int n = m_board.size();
    if (i < 0 || i >= n || j < 0 || j >= n) return;
    // 本地落子不应发送到网络
    tryPlay(i, j, false);
}

void BoardWidget::doAIMove(int difficulty)
{
    QPointer<BoardWidget> guard(this);
    QTimer::singleShot(1, this, [guard, difficulty]() {
        if (!guard) return;
        BoardWidget *self = guard.data();

        int cur = self->m_board.currentPlayer();
        if (difficulty == 0) {
            auto mv = AIRandom::chooseMove(self->m_board, cur);
            if (mv.first == -1) {
                self->m_board.pass();
            } else {
                QString err;
                bool ok = self->m_board.play(mv.first, mv.second, &err);
                Q_UNUSED(ok);
                if (!ok) {
                    // 如果AI由于某种原因落子失败, 则转为虚着
                    self->m_board.pass();
                }
            }
            emit self->stateChanged();
            self->update();
        } else {
            auto mv = AIRandom::chooseMove(self->m_board, cur);
            if (mv.first == -1) self->m_board.pass();
            else {
                QString err; bool ok = self->m_board.play(mv.first, mv.second, &err); Q_UNUSED(ok);
            }
            emit self->stateChanged();
            self->update();
        }
    });
}

void BoardWidget::maybeAIMove()
{
    // 仅在单机模式 (非网络) 且 m_aiEnabled 为 true 时, AI才会自动走棋
    if (!m_aiEnabled) return;
    if (m_networkMode) return;

    int cur = m_board.currentPlayer();

    // 判断是否轮到AI: 如果设置了本地玩家颜色, 则当 cur != localColor 时轮到AI
    // 如果 localColor == 0 (未明确指定人类玩家颜色), 则假定AI为白棋 (原始行为)
    bool aiTurn = false;
    if (m_localColor == 0) {
        aiTurn = (cur == 2);
    } else {
        aiTurn = (cur != m_localColor);
    }

    if (!aiTurn) return;

    // 目前使用难度0 (简单随机)
    doAIMove(0);
}

QPair<int,int> BoardWidget::computeChineseScore()
{
    return m_board.computeChineseScore();
}

const Goban& BoardWidget::goban() const
{
    return m_board;
}
