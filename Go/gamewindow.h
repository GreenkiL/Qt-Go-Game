#include <QWidget>
#include <QJsonObject>
#include "singleplayer.h"

class NetworkManager;
class BoardWidget;
class QLabel;
class QPushButton;
class QTextEdit;
class QLineEdit;

class GameWindow : public QWidget
{
    Q_OBJECT
public:
    GameWindow(NetworkManager *net, const QJsonObject &you, const QJsonObject &roomInfo, QWidget *parent = nullptr);
    ~GameWindow();

signals:
    void exitToLobby();

private slots:
    void onLeaveRoom();
    void onPassClicked();
    void onRequestEndClicked();
    void onResignClicked();
    void onReadyClicked();
    void onJudgeClicked();
    void onNetworkJsonReceived(const QJsonObject &obj);
    void onLogMessage(const QString &msg);
    void onRestartClicked();
    void onChangeSettingsClicked();
    void onAnalysisReady(const QJsonObject& analysisData);

private:
    NetworkManager *m_net;
    QJsonObject m_you;
    QJsonObject m_room;
    BoardWidget *m_board;
    QLabel *m_infoLabel;
    QLabel *m_youLabel;
    QLabel *m_oppLabel;
    QTextEdit *m_chatView;
    QLineEdit *m_chatInput;
    QPushButton *m_sendChatBtn;
    QPushButton *m_passBtn;
    QPushButton *m_requestEndBtn;
    QPushButton *m_resignBtn;
    QPushButton *m_leaveBtn;
    QPushButton *m_readyBtn;   // 准备按钮
    QPushButton *m_judgeBtn;   // 形势判断按钮 (仅本地显示)
    SinglePlayerManager *m_spMgr = nullptr;
    SinglePlayerManager *m_analysisMgr = nullptr; // 专用于形势判断的Manager
    bool m_exiting;
    bool m_isSinglePlayer = false; // 单机模式标识
    QPushButton *m_restartBtn;
    QPushButton *m_changeSettingsBtn;
    // 用于存储单机模式的AI设置
    int m_spAiColor;
    int m_spAiLevel;
};
