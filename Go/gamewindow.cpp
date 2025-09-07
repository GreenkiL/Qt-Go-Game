#include "gamewindow.h"
#include "networkmanager.h"
#include "boardwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QJsonDocument>
#include <QTimer>
#include <QTextEdit>
#include <QLineEdit>
#include <QDateTime>
#include <QInputDialog>
#include <QJsonArray>

GameWindow::GameWindow(NetworkManager *net, const QJsonObject &you, const QJsonObject &roomInfo, QWidget *parent)
    : QWidget(parent), m_net(net), m_you(you), m_room(roomInfo), m_exiting(false)
{
    qDebug() << "[GameWindow] ctor called. roomInfo:" << QJsonDocument(m_room).toJson(QJsonDocument::Compact)
             << " you:" << QJsonDocument(m_you).toJson(QJsonDocument::Compact);

    QVBoxLayout *main = new QVBoxLayout(this);

    // 顶部: 房间和玩家信息
    m_infoLabel = new QLabel(this);
    main->addWidget(m_infoLabel);

    // 我方与对手的详细信息 (段位/胜负等)
    m_youLabel = new QLabel(this);
    m_oppLabel = new QLabel(this);
    main->addWidget(m_youLabel);
    main->addWidget(m_oppLabel);

    // 创建棋盘并立即绑定网络管理器
    m_board = new BoardWidget(this);
    if (m_net) m_board->setNetworkManager(m_net);
    m_board->setNetworkModeEnabled(false);
    main->addWidget(m_board, 1);

    // 控制按钮
    QHBoxLayout *btns = new QHBoxLayout();
    m_readyBtn = new QPushButton(tr("准备"), this);
    m_passBtn = new QPushButton(tr("过"), this);
    m_judgeBtn = new QPushButton(tr("形势判断"), this);
    m_requestEndBtn = new QPushButton(tr("点目"), this);
    m_resignBtn = new QPushButton(tr("认输"), this);
    m_leaveBtn = new QPushButton(tr("离开房间"), this);
    m_restartBtn = new QPushButton(tr("重新开始"), this);
    m_changeSettingsBtn = new QPushButton(tr("更换难度"), this);

    btns->addWidget(m_readyBtn);
    btns->addWidget(m_passBtn);
    btns->addWidget(m_judgeBtn);
    btns->addWidget(m_requestEndBtn);
    btns->addWidget(m_resignBtn);
    btns->addWidget(m_restartBtn);
    btns->addWidget(m_changeSettingsBtn);
    btns->addStretch();
    btns->addWidget(m_leaveBtn);
    main->addLayout(btns);

    connect(m_passBtn, &QPushButton::clicked, this, &GameWindow::onPassClicked);
    connect(m_requestEndBtn, &QPushButton::clicked, this, &GameWindow::onRequestEndClicked);
    connect(m_resignBtn, &QPushButton::clicked, this, &GameWindow::onResignClicked);
    connect(m_leaveBtn, &QPushButton::clicked, this, &GameWindow::onLeaveRoom);
    connect(m_readyBtn, &QPushButton::clicked, this, &GameWindow::onReadyClicked);
    connect(m_judgeBtn, &QPushButton::clicked, this, &GameWindow::onJudgeClicked);
    connect(m_restartBtn, &QPushButton::clicked, this, &GameWindow::onRestartClicked);
    connect(m_changeSettingsBtn, &QPushButton::clicked, this, &GameWindow::onChangeSettingsClicked);

    // 聊天 UI
    m_chatView = new QTextEdit(this);
    m_chatView->setReadOnly(true);
    m_chatInput = new QLineEdit(this);
    m_sendChatBtn = new QPushButton(tr("发送"), this);
    QHBoxLayout *chatRow = new QHBoxLayout();
    chatRow->addWidget(m_chatInput);
    chatRow->addWidget(m_sendChatBtn);
    main->addWidget(new QLabel(tr("房间聊天"), this));
    main->addWidget(m_chatView);
    main->addLayout(chatRow);

    connect(m_sendChatBtn, &QPushButton::clicked, this, [this]() {
        QString txt = this->m_chatInput->text().trimmed();
        if (txt.isEmpty()) return;
        if (!m_net || !m_net->isConnected()) {
            QMessageBox::warning(this, tr("未连接"), tr("无法发送：未连接服务器"));
            return;
        }
        QJsonObject obj;
        obj["type"] = "chat";
        obj["text"] = txt;
        obj["room_id"] = m_room.value("room_id").toString();
        m_net->sendJson(obj);
        m_chatInput->clear();
    });

    // 网络事件 (使用QueuedConnection避免重入)
    if (m_net) {
        connect(m_net, &NetworkManager::jsonReceived, this, &GameWindow::onNetworkJsonReceived, Qt::QueuedConnection);
        connect(m_net, &NetworkManager::logMessage, this, &GameWindow::onLogMessage, Qt::QueuedConnection);
    }

    // 如果房间信息中已分配颜色, 则设置本地玩家颜色
    if (m_room.contains("color")) {
        QString c = m_room.value("color").toString().toLower();
        if (c == "black") m_board->setLocalPlayerColor(1);
        else if (c == "white") m_board->setLocalPlayerColor(2);
    }

    // 初始化一个专门用于形势判断的AI管理器
    m_analysisMgr = new SinglePlayerManager(this);
    m_analysisMgr->attachBoard(m_board);
    // 使用 "level 2" (高级) 启动它, 使其在后台运行 KataGo 进程
    m_analysisMgr->start(0, 2);
    connect(m_analysisMgr, &SinglePlayerManager::analysisReady, this, &GameWindow::onAnalysisReady);

    // UI 初始状态
    m_readyBtn->setEnabled(true);
    m_passBtn->setEnabled(false);
    m_judgeBtn->setEnabled(false);
    m_requestEndBtn->setEnabled(false);
    m_resignBtn->setEnabled(false);
    m_restartBtn->hide();
    m_changeSettingsBtn->hide();

    // 显示初始玩家信息
    auto updatePlayerLabels = [this]() {
        QString yourNick = m_you.value("nickname").toString().isEmpty()
                          ? m_you.value("username").toString() : m_you.value("nickname").toString();
        int yr = m_you.value("rating").toInt();
        int yw = m_you.value("wins").toInt();
        int yl = m_you.value("losses").toInt();
        QString rid =m_room.value("room_id").toString();
        m_infoLabel->setText(tr("房间: %1").arg(rid));
        m_youLabel->setText(tr("你: %1  段位:%2  胜:%3  负:%4").arg(yourNick).arg(yr).arg(yw).arg(yl));

        QJsonObject opp = m_room.value("opponent").isObject() ? m_room.value("opponent").toObject() : QJsonObject();
        if (!opp.isEmpty()) {
            QString oNick = opp.value("nickname").toString().isEmpty() ? opp.value("username").toString() : opp.value("nickname").toString();
            int orat = opp.value("rating").toInt();
            int ow = opp.value("wins").toInt();
            int ol = opp.value("losses").toInt();
            m_oppLabel->setText(tr("对手: %1  段位:%2  胜:%3  负:%4").arg(oNick).arg(orat).arg(ow).arg(ol));
        } else {
            m_oppLabel->setText(tr("对手: (空)"));
        }
    };
    updatePlayerLabels();

    // 如果是单机模式
    if (m_room.contains("singleplayer") && m_room.value("singleplayer").toBool()) {
            m_isSinglePlayer = true;
            if (m_net) {
                disconnect(m_net, nullptr, this, nullptr); // 断开网络连接
            }
            m_board->setNetworkManager(nullptr);
            m_board->setNetworkModeEnabled(false);

            // 创建用于对战的 AI 管理器
            m_spMgr = new SinglePlayerManager(this);
            m_spMgr->attachBoard(m_board);

            // 读取 AI 难度和颜色配置
            int aiLevel = m_room.value("ai_level").toInt(0);
            QString myColor = m_room.value("color").toString().toLower();
            int humanColor = (myColor == "black") ? 1 : 2;
            m_board->setLocalPlayerColor(humanColor);
            int aiColor = (myColor == "black") ? 2 : 1;  // AI 执与玩家相反的颜色

            m_spAiColor = aiColor;
            m_spAiLevel = aiLevel;

            // 将 AI 的走棋信号连接到棋盘的落子槽
            connect(m_spMgr, &SinglePlayerManager::moveReady,
                    m_board, &BoardWidget::playLocalMove,
                    Qt::QueuedConnection);

            // 启动 AI
            m_spMgr->start(aiColor, aiLevel);

            // 启用游戏按钮, 禁用准备按钮
            m_passBtn->setEnabled(true);
            m_judgeBtn->setEnabled(true);
            m_requestEndBtn->setEnabled(true);
            m_resignBtn->setEnabled(true);
            m_readyBtn->setEnabled(false);
            m_infoLabel->setText(tr("单机对局开始: 你执%1")
                                 .arg(myColor=="black"?tr("黑"):tr("白")));

            // 根据难度等级更新对手信息
            QString aiLevelStr;
            switch (aiLevel) {
                case 0: aiLevelStr = tr("初级"); break;
                case 1: aiLevelStr = tr("中级"); break;
                case 2: aiLevelStr = tr("高级"); break;
                default: aiLevelStr = tr("未知"); break;
            }
            m_oppLabel->setText(tr("对手: 电脑 (%1)").arg(aiLevelStr));
    }
}

GameWindow::~GameWindow()
{
    m_exiting = true;
    if (m_net) {
        disconnect(m_net, nullptr, this, nullptr);
    }
    if (m_board) {
        m_board->setNetworkManager(nullptr);
    }
    if (m_spMgr) {
        m_spMgr->stop();
        delete m_spMgr;
        m_spMgr = nullptr;
    }
}

void GameWindow::onPassClicked()
{
    // BoardWidget 内部会处理网络模式下的消息发送
    m_board->playerPass();
}

void GameWindow::onRequestEndClicked()
{
    // 统一由AI分析引擎处理终局点目, 但网络模式需先征得对方同意

    if (m_isSinglePlayer) {
        // 单机模式: 直接请求AI进行终局判断
        qDebug() << "Single player end request -> Performing AI final scoring.";
        if (m_analysisMgr && m_analysisMgr->isRunning()) {
            m_infoLabel->setText(tr("正在请求AI进行终局点目..."));
            m_analysisMgr->requestAnalysis(); // 结果将在 onAnalysisReady 中处理
        } else {
            QMessageBox::warning(this, tr("错误"), tr("分析引擎尚未准备好。"));
        }

        // 游戏结束, 禁用游戏按钮, 显示重开选项
        if (m_spMgr) m_spMgr->stop();
        m_passBtn->hide();
        m_judgeBtn->hide();
        m_requestEndBtn->hide();
        m_resignBtn->hide();
        m_restartBtn->show();
        m_changeSettingsBtn->show();

    } else {
        // 网络模式: 发送点目请求, 等待对方同意
        if (!m_net || !m_net->isConnected()) {
            QMessageBox::warning(this, tr("未连接"), tr("请先连接服务器"));
            return;
        }
        QJsonObject obj;
        obj["type"] = "end_request";
        m_net->sendJson(obj);
        m_infoLabel->setText(tr("已发送点目请求, 等待对方同意..."));
    }
}

void GameWindow::onResignClicked()
{
    if (m_isSinglePlayer) {
        if (m_spMgr) m_spMgr->stop();
        QMessageBox::information(this, tr("认输"), tr("你已认输, AI获胜"));

        // 更新UI, 显示重新开始按钮
        m_passBtn->hide();
        m_judgeBtn->hide();
        m_requestEndBtn->hide();
        m_resignBtn->hide();
        m_restartBtn->show();
        m_changeSettingsBtn->show();
        return;
    }

    // 网络模式下的认输逻辑
    if (!m_net || !m_net->isConnected()) {
        QMessageBox::warning(this, tr("未连接"), tr("请先连接服务器")); return;
    }
    QJsonObject obj; obj["type"]="resign";
    m_net->sendJson(obj);
    QMessageBox::information(this, tr("认输"), tr("你已认输, 游戏结束"));

    // 游戏结束, 回到等待准备状态
    m_board->setNetworkModeEnabled(false);
    m_readyBtn->setEnabled(true);
    m_passBtn->setEnabled(false);
    m_judgeBtn->setEnabled(false);
    m_requestEndBtn->setEnabled(false);
    m_resignBtn->setEnabled(false);
    m_infoLabel->setText(tr("你已认输, 等待双方准备"));
}

void GameWindow::onRestartClicked()
{
    // 恢复UI到游戏进行中状态
    m_restartBtn->hide();
    m_changeSettingsBtn->hide();
    m_passBtn->show();
    m_judgeBtn->show();
    m_requestEndBtn->show();
    m_resignBtn->show();
    m_passBtn->setEnabled(true);
    m_judgeBtn->setEnabled(true);
    m_requestEndBtn->setEnabled(true);
    m_resignBtn->setEnabled(true);

    // 重置棋盘
    m_board->newGame();

    // 重新启动AI
    if (m_spMgr) {
        // 关键: 重新启动前, 需将 Manager 与新的 Board 实例重新关联
        m_spMgr->attachBoard(m_board);
        m_spMgr->start(m_spAiColor, m_spAiLevel);
    }

    // 更新状态信息
    QString myColorStr = (m_spAiColor == 2) ? tr("黑") : tr("白");
    m_infoLabel->setText(tr("单机对局开始: 你执%1").arg(myColorStr));
}

void GameWindow::onChangeSettingsClicked()
{
    // 弹出对话框让用户选择新难度
    QStringList levels = { tr("初级 (随机)"), tr("中级 (传统算法)"), tr("高级 (深度学习)") };
    bool ok = false;
    QString levelChoice = QInputDialog::getItem(this, tr("选择新难度"), tr("请选择AI难度:"), levels, m_spAiLevel, false, &ok);

    if (ok && !levelChoice.isEmpty()) {
        // 更新内部存储的AI难度
        m_spAiLevel = levels.indexOf(levelChoice);

        // 更新对手信息标签
        QString aiLevelStr = levelChoice.split(" ").first(); // e.g. "初级"
        m_oppLabel->setText(tr("对手: 电脑 (%1)").arg(aiLevelStr));

        // 调用“重新开始”函数, 用新设置启动游戏
        onRestartClicked();
    }
}

void GameWindow::onReadyClicked()
{
    if (!m_net || !m_net->isConnected()) {
        QMessageBox::warning(this, tr("未连接"), tr("请先连接服务器")); return;
    }
    // 发送准备消息
    QJsonObject obj; obj["type"] = "ready";
    m_net->sendJson(obj);
    m_readyBtn->setEnabled(false);
    m_infoLabel->setText(tr("已准备, 等待对手..."));
}

void GameWindow::onJudgeClicked()
{
    // 统一使用 m_analysisMgr 进行形势判断
    if (m_analysisMgr && m_analysisMgr->isRunning()) {
        m_infoLabel->setText(tr("正在请求AI进行形势判断..."));
        m_analysisMgr->requestAnalysis();
    } else {
        QMessageBox::warning(this, tr("错误"), tr("分析引擎尚未准备好。"));
    }
}

void GameWindow::onAnalysisReady(const QJsonObject &analysisData)
{
    m_infoLabel->setText(tr("AI分析完成！"));

    // 解析所有权数据用于棋盘可视化
    QJsonArray ownershipArr = analysisData.value("ownership").toArray();
    QVector<double> ownershipMap;
    for (const auto& val : ownershipArr) {
        ownershipMap.append(val.toDouble());
    }
    m_board->displayAnalysis(ownershipMap);

    // 从 "rootInfo" 子对象中读取分数
    QJsonObject rootInfo = analysisData.value("rootInfo").toObject();
    double blackScore = rootInfo.value("scoreLead").toDouble();

    // 在消息框中显示结果
    QString msg = tr("AI 形势判断结果：\n\n");
    if (blackScore > 0.1) {
        msg += tr("黑棋领先 %1 目").arg(QString::number(blackScore, 'f', 1));
    } else if (blackScore < -0.1) {
        msg += tr("白棋领先 %1 目").arg(QString::number(-blackScore, 'f', 1));
    } else {
        msg += tr("局势平稳, 胜负接近");
    }

    QMessageBox::information(this, tr("形势判断"), msg);
}

void GameWindow::onNetworkJsonReceived(const QJsonObject &obj)
{
    if (m_exiting) return;
    QString t = obj.value("type").toString();

    if (t == "start") {
        if (m_net) m_board->setNetworkManager(m_net);
        QString color = obj.value("color").toString().toLower();
        int c = (color == "black") ? 1 : 2;
        m_board->newGame();
        m_board->setLocalPlayerColor(c);
        m_board->setNetworkModeEnabled(true);
        m_passBtn->setEnabled(true);
        m_judgeBtn->setEnabled(true);
        m_requestEndBtn->setEnabled(true);
        m_resignBtn->setEnabled(true);
        m_readyBtn->setEnabled(false);
        m_infoLabel->setText(tr("对局开始: 你执%1").arg(c==1?tr("黑"):tr("白")));
        return;
    }

    if (t == "player_ready") {
        m_infoLabel->setText(tr("对手已准备"));
        return;
    }

    if (t == "end_request") {
        // 显示对方的点目请求
        QString prompt = tr("对方请求结束对局并点目, 是否同意？");
        QMessageBox::StandardButton rb = QMessageBox::question(this, tr("点目请求"), prompt, QMessageBox::Yes | QMessageBox::No);
        if (rb == QMessageBox::Yes) {
            QJsonObject conf; conf["type"] = "end_confirm";
            m_net->sendJson(conf);
            m_infoLabel->setText(tr("你已同意点目"));
        } else {
            QJsonObject decline; decline["type"] = "end_decline";
            m_net->sendJson(decline);
            m_infoLabel->setText(tr("你已拒绝点目请求"));
        }
        return;
    }

    if (t == "end_confirm") {
        // 收到双方同意结束的消息, 请求AI进行最终计分
        if (m_analysisMgr && m_analysisMgr->isRunning()) {
            m_infoLabel->setText(tr("双方同意结束, 正在请求AI点目..."));
            m_analysisMgr->requestAnalysis(); // 结果将在 onAnalysisReady 中显示
        } else {
            // 若分析引擎出错, 则使用旧的、不精确的计分方法作为后备
            QMessageBox::warning(this, tr("警告"), tr("分析引擎不可用, 将使用旧的计分方法。"));
            auto score = m_board->computeChineseScore();
            int black = score.first, white = score.second;
            QString msg = tr("点目结束（双方同意）\n黑子: %1\n白子: %2\n").arg(black).arg(white);
            if (black > white) msg += tr("\n结果: 黑方获胜");
            else if (white > black) msg += tr("\n结果: 白方获胜");
            else msg += tr("\n结果: 平局");
            QMessageBox::information(this, tr("对局结束 - 点目"), msg);
        }

        // 游戏结束, UI回到准备状态
        m_board->setNetworkModeEnabled(false);
        m_readyBtn->setEnabled(true);
        m_passBtn->setEnabled(false);
        m_judgeBtn->setEnabled(false);
        m_requestEndBtn->setEnabled(false);
        m_resignBtn->setEnabled(false);
        m_infoLabel->setText(tr("对局结束, 等待双方准备"));
        return;
    }

    if (t == "resign") {
        QMessageBox::information(this, tr("消息"), tr("对方认输, 你获胜"));
        m_board->setNetworkModeEnabled(false);
        m_readyBtn->setEnabled(true);
        m_passBtn->setEnabled(false);
        m_judgeBtn->setEnabled(false);
        m_requestEndBtn->setEnabled(false);
        m_resignBtn->setEnabled(false);
        m_infoLabel->setText(tr("对手认输, 等待双方准备"));
        return;
    }

    if (t == "opponent_left") {
        QMessageBox::information(this, tr("对手离开"), tr("对手已离开房间"));
        if (m_net) disconnect(m_net, nullptr, this, nullptr);
        if (m_board) m_board->setNetworkManager(nullptr);
        QTimer::singleShot(0, this, [this]() { emit exitToLobby(); });
        return;
    }

    if (t == "opponent_joined") {
        QJsonObject opp = obj.value("opponent").isObject() ? obj.value("opponent").toObject() : QJsonObject();
        if (!opp.isEmpty()) {
            m_room["opponent"] = opp;
            // 更新对手信息标签
            QString oNick = opp.value("nickname").toString().isEmpty() ? opp.value("username").toString() : opp.value("nickname").toString();
            int orat = opp.value("rating").toInt();
            int ow = opp.value("wins").toInt();
            int ol = opp.value("losses").toInt();
            m_oppLabel->setText(tr("对手: %1  段位:%2  胜:%3  负:%4").arg(oNick).arg(orat).arg(ow).arg(ol));
        }
        return;
    }

    // "room_joined" 或 "matched" 消息处理
    if (t == "room_joined" || t == "matched") {
        if (obj.contains("you") && obj.value("you").isObject()) m_you = obj.value("you").toObject();
        if (obj.contains("opponent") && obj.value("opponent").isObject()) m_room["opponent"] = obj.value("opponent").toObject();

        // 更新UI标签
        QString roomId = obj.value("room_id").toString();
        m_room["room_id"] = roomId;
        m_infoLabel->setText(tr("房间: %1").arg(roomId));
        QString yourNick = m_you.value("nickname").toString().isEmpty() ? m_you.value("username").toString() : m_you.value("nickname").toString();

        QString yourDetail = tr("你: %1  段位:%2  胜:%3  负:%4")
                .arg(yourNick).arg(m_you.value("rating").toInt()).arg(m_you.value("wins").toInt()).arg(m_you.value("losses").toInt());
        m_youLabel->setText(yourDetail);

        QJsonObject opp = m_room.value("opponent").isObject() ? m_room.value("opponent").toObject() : QJsonObject();
        if (!opp.isEmpty()) {
            QString oNick = opp.value("nickname").toString().isEmpty() ? opp.value("username").toString() : opp.value("nickname").toString();
            QString oppDetail = tr("对手: %1  段位:%2  胜:%3  负:%4").arg(oNick).arg(opp.value("rating").toInt()).arg(opp.value("wins").toInt()).arg(opp.value("losses").toInt());
            m_oppLabel->setText(oppDetail);
        } else {
            m_oppLabel->setText(tr("对手: (空)"));
        }

        // 如果消息包含颜色信息, 则设置
        if (obj.contains("color")) {
            QString c = obj.value("color").toString().toLower();
            if (c == "black") m_board->setLocalPlayerColor(1);
            else if (c == "white") m_board->setLocalPlayerColor(2);
        }

        // 重置UI为准备状态
        m_readyBtn->setEnabled(true);
        m_passBtn->setEnabled(false);
        m_judgeBtn->setEnabled(false);
        m_requestEndBtn->setEnabled(false);
        m_resignBtn->setEnabled(false);

        return;
    }

    // 聊天消息处理
    if (t == "chat") {
        QString from = obj.value("from").toString();
        QString text = obj.value("text").toString();
        QString time = obj.contains("time") ? obj.value("time").toString() : QDateTime::currentDateTime().toString();
        QString line = QString("[%1] %2: %3").arg(time).arg(from).arg(text);
        m_chatView->append(line);
        return;
    }

    // 提示类消息 (move/pass)
    if (t == "move") {
        m_infoLabel->setText(tr("收到对手落子"));
        return;
    }
    if (t == "pass") {
        m_infoLabel->setText(tr("对手已虚着"));
        return;
    }

    if (t == "error") {
        QString msg = obj.value("msg").toString();
        QMessageBox::warning(this, tr("服务器错误"), msg);
        return;
    }
}

void GameWindow::onLeaveRoom()
{
    if (m_exiting) return;
    m_exiting = true;
    if (m_net && m_net->isConnected()) {
        QJsonObject obj; obj["type"] = "leave";
        m_net->sendJson(obj);
    }
    if (m_net) disconnect(m_net, nullptr, this, nullptr);
    if (m_board) m_board->setNetworkManager(nullptr);
    QTimer::singleShot(0, this, [this]() { emit exitToLobby(); });
}

void GameWindow::onLogMessage(const QString &msg)
{
    // 可选地在状态栏或标签中显示日志
    m_infoLabel->setText(tr("房间: %1\n你: %2\n状态: %3")
                         .arg(m_room.value("room_id").toString())
                         .arg(m_you.value("nickname").toString())
                         .arg(msg));
}
