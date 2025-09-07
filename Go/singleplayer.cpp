#include "singleplayer.h"
#include "boardwidget.h"
#include "ai_random.h"

#include <QTimer>
#include <QDebug>
#include <QProcess>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QJsonArray>
#include <random>
#include <set>

SinglePlayerManager::SinglePlayerManager(QObject *parent)
    : QObject(parent),
      m_board(nullptr),
      m_aiColor(0),
      m_aiLevel(0),
      m_running(false),
      m_timer(new QTimer(this)),
      m_kataGoProcess(nullptr),
      m_kataGoBuffer()
{
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &SinglePlayerManager::onTimerTimeout);
}

SinglePlayerManager::~SinglePlayerManager()
{
    stop();
}

void SinglePlayerManager::attachBoard(BoardWidget *board)
{
    if (m_board == board) return;

    if (m_board) {
        disconnect(m_board, &BoardWidget::stateChanged, this, &SinglePlayerManager::onBoardStateChanged);
    }

    m_board = board;
    if (m_board) {
        connect(m_board, &BoardWidget::stateChanged, this, &SinglePlayerManager::onBoardStateChanged, Qt::QueuedConnection);
    }
}

void SinglePlayerManager::start(int aiColor, int level)
{
    m_aiColor = (aiColor >= 0 && aiColor <= 2) ? aiColor : 0;
    m_aiLevel = qBound(0, level, 2);
    m_running = true;

    if (m_aiLevel == 2) {
        if (!m_kataGoProcess) {
            m_kataGoProcess = new QProcess(this);
            connect(m_kataGoProcess, &QProcess::readyReadStandardOutput, this, &SinglePlayerManager::onKataGoReadyRead);
            connect(m_kataGoProcess, &QProcess::errorOccurred, this, &SinglePlayerManager::onKataGoError);
            m_kataGoProcess->setProcessChannelMode(QProcess::MergedChannels);
        }

        if (m_kataGoProcess->state() != QProcess::NotRunning) {
            m_kataGoProcess->kill();
            m_kataGoProcess->waitForFinished();
        }

        QString appDir = QCoreApplication::applicationDirPath();
        QString kataGoDir = appDir + "/katago/";
        QDir dir(kataGoDir);

        if (!dir.exists()) {
            qDebug() << "--- 致命错误: KataGo 目录未找到于:" << kataGoDir;
            m_running = false;
            return;
        }

        QString kataGoExePath = kataGoDir + "katago.exe";
        QString modelPath = "model.bin";
        QString configPath = "gtp_config.cfg";

        if (!QFile::exists(kataGoExePath)) {
            qDebug() << "--- 致命错误: katago.exe 未找到于:" << kataGoExePath;
            m_running = false;
            return;
        }

        m_kataGoProcess->setWorkingDirectory(kataGoDir);

        QStringList arguments;
        if (m_aiColor == 0) { // 分析引擎
            qDebug() << "以分析模式启动 KataGo...";
            arguments << "analysis" << "-model" << modelPath << "-config" << configPath;
        } else { // 对弈引擎
            qDebug() << "以 GTP 对弈模式启动 KataGo...";
            arguments << "gtp" << "-model" << modelPath << "-config" << configPath;
        }

        qDebug() << "工作目录:" << kataGoDir;
        qDebug() << "可执行文件:" << kataGoExePath;
        qDebug() << "参数:" << arguments.join(" ");

        m_kataGoProcess->start(kataGoExePath, arguments);

        if (!m_kataGoProcess->waitForStarted(5000)) {
            qDebug() << "--- KATA GO 启动失败 ---";
            qDebug() << "错误码:" << m_kataGoProcess->error();
            qDebug() << "错误信息:" << m_kataGoProcess->errorString();
        } else {
            qDebug() << "KataGo 进程启动成功。";
            if (m_aiColor != 0) {
                const QByteArray boardsizeCmd = QString("boardsize %1\n").arg(m_board->goban().size()).toUtf8();
                m_kataGoProcess->write(boardsizeCmd);
                m_kataGoProcess->write("clear_board\n");
            }
        }
    }

    // 仅当作为对弈AI时, 才启动定时器检查
    if(m_aiColor != 0) {
         QTimer::singleShot(0, this, &SinglePlayerManager::onBoardStateChanged);
    }
    qDebug() << "[SinglePlayer] 已启动: color=" << m_aiColor << " level=" << m_aiLevel;
}

void SinglePlayerManager::stop()
{
    m_running = false;
    if (m_timer->isActive()) m_timer->stop();
    if (m_board) {
        disconnect(m_board, &BoardWidget::stateChanged, this, &SinglePlayerManager::onBoardStateChanged);
    }
    m_board = nullptr;

    if (m_kataGoProcess && m_kataGoProcess->state() == QProcess::Running) {
        m_kataGoProcess->write("quit\n");
        if (!m_kataGoProcess->waitForFinished(1000)) {
            m_kataGoProcess->kill();
        }
    }
    delete m_kataGoProcess;
    m_kataGoProcess = nullptr;

    qDebug() << "[SinglePlayer] 已停止";
}

void SinglePlayerManager::onBoardStateChanged()
{
    if (!m_running || !m_board) return;
    if (m_aiColor == 0) return; // 分析引擎模式下不响应棋盘变化
    int cur = m_board->currentPlayer();
    if (cur != m_aiColor) return;
    if (!m_timer->isActive()) {
        int delay = (m_aiLevel <= 0) ? 120 : 250;
        m_timer->start(delay);
    }
}

void SinglePlayerManager::onTimerTimeout()
{
    if (!m_running || !m_board || m_board->currentPlayer() != m_aiColor) return;
    if (m_aiLevel < 2) {
        QPair<int,int> mv = chooseMoveLvl0_1();
        emit moveReady(mv.first, mv.second);
    } else {
        requestKataGoMove();
    }
}

void SinglePlayerManager::requestKataGoMove()
{
    if (!m_kataGoProcess || m_kataGoProcess->state() != QProcess::Running) {
        qDebug() << "KataGo (GTP 模式) 未运行!";
        emit moveReady(-1, -1);
        return;
    }

    const Goban& g = m_board->goban();
    m_kataGoProcess->write("clear_board\n");

    QByteArray playCommands;
    for(int i = 0; i < g.size(); ++i) {
        for(int j = 0; j < g.size(); ++j) {
            if (g.get(i, j) != 0) {
                QString color = (g.get(i, j) == 1) ? "B" : "W";
                char colChar = 'A' + j;
                if (colChar >= 'I') colChar++;
                int row = g.size() - i;
                playCommands.append(QString("play %1 %2%3\n").arg(color).arg(colChar).arg(row).toUtf8());
            }
        }
    }
    m_kataGoProcess->write(playCommands);

    QString player = (m_aiColor == 1) ? "B" : "W";
    const QByteArray genmoveCommand = QString("genmove %1\n").arg(player).toUtf8();
    qDebug() << "发送到 KataGo (GTP):" << QString::fromUtf8(genmoveCommand).trimmed();
    m_kataGoProcess->write(genmoveCommand);
}

// 这是回退到的 "Talking Nonsense" 版本函数
void SinglePlayerManager::requestAnalysis()
{
    if (!m_kataGoProcess || m_kataGoProcess->state() != QProcess::Running) {
        qDebug() << "分析引擎未运行!";
        return;
    }

    const Goban& g = m_board->goban();

    // --- 核心修复：从真实的下棋历史构建 moves 数组 ---
    QJsonArray movesArray;
    const auto& moveHistory = g.getMoveHistory(); // 获取准确的历史记录

    for (const auto& moveRecord : moveHistory) {
        const auto& pos = moveRecord.first;
        int colorInt = moveRecord.second;

        QString player = (colorInt == 1) ? "B" : "W";
        char colChar = 'A' + pos.second; // pos.second 是 j 坐标
        if (colChar >= 'I') colChar++;
        int row = g.size() - pos.first; // pos.first 是 i 坐标
        QString moveStr = QString(colChar) + QString::number(row);

        QJsonArray moveJson;
        moveJson.append(player);
        moveJson.append(moveStr);
        movesArray.append(moveJson);
    }
    // --- 修复结束 ---

    QJsonObject request;
    request["id"] = "qt_analysis_query";
    request["moves"] = movesArray; // 现在这里包含了正确的、有顺序的历史
    request["rules"] = "tromp-taylor";
    request["komi"] = 7.5;
    request["boardXSize"] = g.size();
    request["boardYSize"] = g.size();
    request["maxVisits"] = 200;
    request["includeOwnership"] = true;

    QJsonDocument doc(request);
    QByteArray jsonQuery = doc.toJson(QJsonDocument::Compact);
    qDebug() << "发送修正后的分析请求:" << QString::fromUtf8(jsonQuery);
    m_kataGoProcess->write(jsonQuery + "\n");
}


void SinglePlayerManager::onKataGoReadyRead()
{
    m_kataGoBuffer.append(m_kataGoProcess->readAllStandardOutput());
    while (m_kataGoBuffer.contains('\n')) {
        int newlinePos = m_kataGoBuffer.indexOf('\n');
        QByteArray line = m_kataGoBuffer.left(newlinePos);
        m_kataGoBuffer.remove(0, newlinePos + 1);

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(line, &error);

        if (error.error == QJsonParseError::NoError && doc.isObject()) {
            QJsonObject obj = doc.object();
            if(obj.value("id").toString() == "qt_analysis_query") {
                if (obj.contains("ownership")) {
                    qDebug() << "成功解析含所有权数据的分析JSON。";
                    emit analysisReady(obj);
                    return;
                }
            }
        } else {
            QString response = QString::fromLatin1(line).trimmed();
            if (response.startsWith("=")) {
                QString moveStr = response.section(' ', 1, 1).toUpper();
                if (moveStr.isEmpty()) continue;
                if (moveStr == "PASS" || moveStr == "RESIGN") {
                    emit moveReady(-1, -1);
                    return;
                }
                if (moveStr.length() >= 2) {
                    int j = moveStr[0].toLatin1() - 'A';
                    if (moveStr[0].toLatin1() > 'I') j--;
                    bool ok;
                    int row = moveStr.mid(1).toInt(&ok);
                    if(ok){
                        int i = m_board->goban().size() - row;
                        qDebug() << "成功解析落子:" << moveStr << "-> (" << i << "," << j << ")";
                        emit moveReady(i, j);
                        return;
                    }
                }
            }
        }
    }
}

void SinglePlayerManager::onKataGoError(QProcess::ProcessError error)
{
    qDebug() << "KataGo 进程错误:" << error << m_kataGoProcess->errorString();
}

QPair<int,int> SinglePlayerManager::chooseMoveLvl0_1()
{
    if (!m_board) {
        qDebug() << "[SinglePlayer] chooseMove 错误: board 为空";
        return QPair<int,int>(-1, -1);
    }
    if (m_aiLevel == 1) {
        qDebug() << "[SinglePlayer Lvl 1] 评估落子...";
        const Goban& currentGoban = m_board->goban();
        int aiColor = m_aiColor;
        int humanColor = 3 - aiColor;
        auto legalMoves = currentGoban.legalMoves(aiColor);
        if (legalMoves.empty()) {
            return QPair<int,int>(-1, -1);
        }
        std::pair<int, int> initialMove = legalMoves[0];
        QPair<int, int> bestMove(initialMove.first, initialMove.second);
        double bestScore = -1.0;
        std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<> distrib(0.1, 0.5);
        std::set<std::pair<int, int>> savingMoves;
        std::vector<char> visited(currentGoban.size() * currentGoban.size(), 0);
        for (int i = 0; i < currentGoban.size(); ++i) {
            for (int j = 0; j < currentGoban.size(); ++j) {
                if (currentGoban.get(i, j) == aiColor && !visited[i * currentGoban.size() + j]) {
                    auto groupInfo = currentGoban.getGroupInfo(i, j);
                    for (const auto& stone : groupInfo.first) {
                        visited[stone.first * currentGoban.size() + stone.second] = 1;
                    }
                    if (groupInfo.second == 1) {
                        for (const auto& stone : groupInfo.first) {
                            for (const auto& nb : currentGoban.neighbors(stone.first, stone.second)) {
                                if (currentGoban.get(nb.first, nb.second) == 0) {
                                    savingMoves.insert(nb);
                                    goto next_group_check;
                                }
                            }
                        }
                    }
                }
                next_group_check:;
            }
        }
        for (const auto& move : legalMoves) {
            double currentScore = distrib(rng);
            Goban tempGoban = currentGoban;
            int humanStonesBefore = 0;
            for(int r=0; r < tempGoban.size(); ++r) for(int c=0; c < tempGoban.size(); ++c) if(tempGoban.get(r, c) == humanColor) humanStonesBefore++;
            QString error;
            tempGoban.play(move.first, move.second, &error);
            int humanStonesAfter = 0;
            for(int r=0; r < tempGoban.size(); ++r) for(int c=0; c < tempGoban.size(); ++c) if(tempGoban.get(r, c) == humanColor) humanStonesAfter++;
            int capturedCount = humanStonesBefore - humanStonesAfter;
            if (capturedCount > 0) {
                currentScore += 1000.0 * capturedCount;
            }
            if (savingMoves.count(move)) {
                currentScore += 800.0;
            }
            for (auto const& nb : tempGoban.neighbors(move.first, move.second)) {
                if (tempGoban.get(nb.first, nb.second) == humanColor) {
                    if (tempGoban.getGroupInfo(nb.first, nb.second).second == 1) {
                        currentScore += 100.0;
                        break;
                    }
                }
            }
            for (auto const& nb : currentGoban.neighbors(move.first, move.second)) {
                if (currentGoban.get(nb.first, nb.second) == aiColor) currentScore += 2.0;
                else if (currentGoban.get(nb.first, nb.second) == humanColor) currentScore += 1.0;
            }
            if (currentScore > bestScore) {
                bestScore = currentScore;
                bestMove.first = move.first;
                bestMove.second = move.second;
            }
        }
        qDebug() << "[SinglePlayer Lvl 1] 最佳落子:" << bestMove.first << "," << bestMove.second << " 分数:" << bestScore;
        return bestMove;
    }
    else if (m_aiLevel == 0) {
        qDebug() << "[SinglePlayer Lvl 0] 随机选择落子...";
        std::pair<int, int> move = AIRandom::chooseMove(m_board->goban(), m_aiColor);
        return QPair<int, int>(move.first, move.second);
    }
    return QPair<int, int>(-1, -1);
}
