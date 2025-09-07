#ifndef GOBAN_H
#define GOBAN_H

#include <vector>
#include <QString>
#include <QPair>

/*
 Goban: 管理棋盘状态与规则（含提子、自杀、即时劫(禁止回到上一个历史局面)）
*/

class Goban
{
public:
    explicit Goban(int n = 19);

    int size() const { return m_n; }
    int get(int i, int j) const; // 0: 空, 1: 黑, 2: 白
    int currentPlayer() const { return m_cur; }

    // 尝试落子, 成功返回true, 失败则在err中写入错误信息
    bool play(int i, int j, QString *err = nullptr);

    // 虚着
    void pass();

    // 重置棋局
    void reset();

    // 计算中国规则下的得分 (子数 + 目数)
    QPair<int,int> computeChineseScore() const;

    // 获取指定颜色的所有合法落子点 (不含虚着)
    std::vector<std::pair<int,int>> legalMoves(int color) const;

    // 棋盘序列化, 用于网络同步
    std::string serialize() const;
    // 从序列化数据加载棋盘 (若棋盘尺寸不匹配则返回 false)
    bool deserialize(const std::string &s);

    // 设置当前玩家 (用于网络同步)
    void setCurrentPlayer(int p);

    // 获取指定位置棋子所在的棋块信息 <棋块坐标, 气数>
    QPair<std::vector<std::pair<int,int>>, int> getGroupInfo(int i, int j) const;
    // 获取一个点的相邻点
    std::vector<std::pair<int,int>> neighbors(int i,int j) const;
    // 获取历史手数记录
    const std::vector<std::pair<std::pair<int, int>, int>>& getMoveHistory() const;

private:
    int m_n;
    int m_cur;
    std::vector<int> m_board; // 行主序存储, m_board[i*m_n + j]

    // 序列化历史, 用于实现简单的劫争规则 (禁止盘面即时重复)
    std::string m_prevSerialized;
    std::string m_lastSerialized;
    std::vector<std::pair<std::pair<int, int>, int>> m_moveHistory;

    // 辅助函数
    int idx(int i,int j) const { return i*m_n + j; }
    bool inBoard(int i,int j) const { return i>=0 && j>=0 && i<m_n && j<m_n; }

    // 棋块搜索与气数计算
    void floodGroupOnVector(const std::vector<int> &boardVec, int si, int sj, std::vector<char> &visited, std::vector<std::pair<int,int>> &outGroup, int &liberties) const;

    // 从棋盘向量中移除一个棋块 (提子)
    void removeGroupOnVector(std::vector<int> &boardVec, const std::vector<std::pair<int,int>> &group) const;
};

#endif // GOBAN_H
