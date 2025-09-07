#include "goban.h"
#include <queue>
#include <set>
#include <algorithm>

Goban::Goban(int n)
    : m_n(n), m_cur(1), m_board(n*n, 0)
{
}

int Goban::get(int i, int j) const
{
    if (!inBoard(i,j)) return -1;
    return m_board[idx(i,j)];
}

void Goban::reset()
{
    std::fill(m_board.begin(), m_board.end(), 0);
    m_prevSerialized.clear();
    m_lastSerialized.clear();
    m_moveHistory.clear();
    m_cur = 1;
}

void Goban::setCurrentPlayer(int p)
{
    if (p == 1 || p == 2) m_cur = p;
}

void Goban::pass()
{
    // 为劫争逻辑记录历史盘面
    m_prevSerialized = m_lastSerialized;
    m_lastSerialized = serialize();

    m_cur = 3 - m_cur;
}

std::vector<std::pair<int,int>> Goban::neighbors(int i,int j) const
{
    std::vector<std::pair<int,int>> r;
    if (i>0) r.emplace_back(i-1,j);
    if (i<m_n-1) r.emplace_back(i+1,j);
    if (j>0) r.emplace_back(i,j-1);
    if (j<m_n-1) r.emplace_back(i,j+1);
    return r;
}

void Goban::floodGroupOnVector(const std::vector<int> &boardVec, int si, int sj, std::vector<char> &visited, std::vector<std::pair<int,int>> &outGroup, int &liberties) const
{
    int color = boardVec[idx(si,sj)];
    std::queue<std::pair<int,int>> q;
    q.push({si,sj});
    visited[idx(si,sj)] = 1;
    liberties = 0;
    while (!q.empty()) {
        auto cur = q.front(); q.pop();
        outGroup.push_back(cur);
        for (auto nb : neighbors(cur.first, cur.second)) {
            int v = boardVec[idx(nb.first, nb.second)];
            if (v == 0) liberties++;
            else if (v == color) {
                if (!visited[idx(nb.first, nb.second)]) {
                    visited[idx(nb.first, nb.second)] = 1;
                    q.push(nb);
                }
            }
        }
    }
}

void Goban::removeGroupOnVector(std::vector<int> &boardVec, const std::vector<std::pair<int,int>> &group) const
{
    for (auto &p : group) {
        boardVec[idx(p.first,p.second)] = 0;
    }
}

bool Goban::play(int i, int j, QString *err)
{
    if (!inBoard(i,j)) {
        if (err) *err = "坐标越界";
        return false;
    }
    if (get(i,j) != 0) {
        if (err) *err = "该点已有棋子";
        return false;
    }

    // 在棋盘副本上进行模拟
    std::vector<int> copy = m_board;
    auto setAt = [&](int a,int b,int val){ copy[idx(a,b)] = val; };
    setAt(i,j, m_cur);

    // 提掉相邻的、没有气的对方棋块
    int opp = 3 - m_cur;
    std::vector<std::pair<int,int>> toRemove;
    std::vector<char> visited(copy.size(), 0);
    for (auto nb : neighbors(i,j)) {
        if (copy[idx(nb.first, nb.second)] == opp) {
            if (!visited[idx(nb.first, nb.second)]) {
                std::vector<std::pair<int,int>> group;
                int libs = 0;
                // 在副本上进行广度优先搜索
                std::queue<std::pair<int,int>> q;
                q.push(nb);
                visited[idx(nb.first, nb.second)] = 1;
                while (!q.empty()) {
                    auto cur = q.front(); q.pop();
                    group.push_back(cur);
                    for (auto n2 : neighbors(cur.first, cur.second)) {
                        int val = copy[idx(n2.first, n2.second)];
                        if (val == 0) libs++;
                        else if (val == opp) {
                            if (!visited[idx(n2.first, n2.second)]) {
                                visited[idx(n2.first, n2.second)] = 1;
                                q.push(n2);
                            }
                        }
                    }
                }
                if (libs == 0) {
                    // 标记为待提子
                    toRemove.insert(toRemove.end(), group.begin(), group.end());
                }
            }
        }
    }

    // 执行提子
    for (auto &p : toRemove) copy[idx(p.first, p.second)] = 0;

    // 检查落子后, 己方棋块是否有气 (禁自杀)
    std::vector<char> visited2(m_board.size(), 0);
    std::vector<std::pair<int,int>> myGroup;
    int myLibs = 0;
    std::queue<std::pair<int,int>> q;
    q.push({i,j});
    visited2[idx(i,j)] = 1;
    while (!q.empty()) {
        auto cur = q.front(); q.pop();
        myGroup.push_back(cur);
        for (auto nb : neighbors(cur.first, cur.second)) {
            int val = copy[idx(nb.first, nb.second)];
            if (val == 0) myLibs++;
            else if (val == m_cur) {
                if (!visited2[idx(nb.first, nb.second)]) {
                    visited2[idx(nb.first, nb.second)] = 1;
                    q.push(nb);
                }
            }
        }
    }
    if (myLibs == 0) {
        if (err) *err = "禁止自杀";
        return false;
    }

    // 简单劫争检查: 新盘面不能等于上上手的盘面
    std::string newSer;
    newSer.reserve(copy.size());
    for (auto v : copy) newSer.push_back(char('0' + v));
    if (!m_prevSerialized.empty() && newSer == m_prevSerialized) {
        if (err) *err = "违反劫争规则";
        return false;
    }

    m_moveHistory.push_back({{i, j}, m_cur});

    // 将模拟结果应用到实际棋盘
    m_board.swap(copy);

    // 更新历史记录, 用于劫争判断
    m_prevSerialized = m_lastSerialized;
    m_lastSerialized = serialize();

    // 交换棋手
    m_cur = 3 - m_cur;
    return true;
}

std::vector<std::pair<int,int>> Goban::legalMoves(int color) const
{
    std::vector<std::pair<int,int>> out;
    for (int i = 0; i < m_n; ++i) {
        for (int j = 0; j < m_n; ++j) {
            if (get(i,j) != 0) continue;
            // 模拟落子, 过程类似 play 函数
            std::vector<int> copy = m_board;
            auto setAt = [&](int a,int b,int val){ copy[idx(a,b)] = val; };
            setAt(i,j, color);

            int opp = 3 - color;
            // 处理提子
            std::vector<char> visited(copy.size(), 0);
            for (auto nb : neighbors(i,j)) {
                if (copy[idx(nb.first, nb.second)] == opp && !visited[idx(nb.first, nb.second)]) {
                    std::vector<std::pair<int,int>> group;
                    int libs = 0;
                    std::queue<std::pair<int,int>> q;
                    q.push(nb);
                    visited[idx(nb.first, nb.second)] = 1;
                    while (!q.empty()) {
                        auto cur = q.front(); q.pop();
                        group.push_back(cur);
                        for (auto n2 : neighbors(cur.first, cur.second)) {
                            int val = copy[idx(n2.first, n2.second)];
                            if (val == 0) libs++;
                            else if (val == opp) {
                                if (!visited[idx(n2.first, n2.second)]) {
                                    visited[idx(n2.first, n2.second)] = 1;
                                    q.push(n2);
                                }
                            }
                        }
                    }
                    if (libs == 0) {
                        for (auto &p : group) copy[idx(p.first,p.second)] = 0;
                    }
                }
            }

            // 检查自杀点
            std::vector<char> visited2(copy.size(), 0);
            std::queue<std::pair<int,int>> q2;
            q2.push({i,j});
            visited2[idx(i,j)] = 1;
            bool hasLib = false;
            while (!q2.empty() && !hasLib) {
                auto cur = q2.front(); q2.pop();
                for (auto nb : neighbors(cur.first, cur.second)) {
                    int val = copy[idx(nb.first, nb.second)];
                    if (val == 0) { hasLib = true; break; }
                    else if (val == color) {
                        if (!visited2[idx(nb.first, nb.second)]) {
                            visited2[idx(nb.first, nb.second)] = 1;
                            q2.push(nb);
                        }
                    }
                }
            }
            if (!hasLib) continue;

            // 检查劫争
            std::string newSer;
            newSer.reserve(copy.size());
            for (auto v : copy) newSer.push_back(char('0' + v));
            if (!m_prevSerialized.empty() && newSer == m_prevSerialized) continue;

            out.emplace_back(i,j);
        }
    }
    return out;
}

QPair<int,int> Goban::computeChineseScore() const
{
    int blackStones = 0, whiteStones = 0;
    for (int v : m_board) {
        if (v == 1) blackStones++;
        else if (v == 2) whiteStones++;
    }

    int territoryBlack = 0, territoryWhite = 0;
    std::vector<char> seen(m_board.size(), 0);
    for (int i = 0; i < m_n; ++i) {
        for (int j = 0; j < m_n; ++j) {
            if (get(i,j) != 0) continue;
            if (seen[idx(i,j)]) continue;

            // 从一个空点开始泛洪, 确定该区域归属
            std::queue<std::pair<int,int>> q;
            std::vector<std::pair<int,int>> region;
            q.push({i,j});
            seen[idx(i,j)] = 1;
            bool borderBlack = false, borderWhite = false;
            while (!q.empty()) {
                auto cur = q.front(); q.pop();
                region.push_back(cur);
                for (auto nb : neighbors(cur.first, cur.second)) {
                    int val = get(nb.first, nb.second);
                    if (val == 0) {
                        if (!seen[idx(nb.first, nb.second)]) {
                            seen[idx(nb.first, nb.second)] = 1;
                            q.push(nb);
                        }
                    } else if (val == 1) borderBlack = true;
                    else if (val == 2) borderWhite = true;
                }
            }
            if (borderBlack && !borderWhite) territoryBlack += (int)region.size();
            else if (borderWhite && !borderBlack) territoryWhite += (int)region.size();
        }
    }

    return qMakePair(blackStones + territoryBlack, whiteStones + territoryWhite);
}

std::string Goban::serialize() const
{
    std::string s;
    s.reserve(m_board.size());
    for (auto v : m_board) s.push_back(char('0' + v));
    return s;
}

bool Goban::deserialize(const std::string &s)
{
    if ((int)s.size() != m_n*m_n) return false;
    for (int i = 0; i < m_n; ++i) {
        for (int j = 0; j < m_n; ++j) {
            m_board[idx(i,j)] = int(s[i*m_n + j] - '0');
        }
    }
    // 重置历史记录, 避免同步后出现错误的劫争判断
    m_prevSerialized.clear();
    m_lastSerialized = serialize();
    return true;
}

QPair<std::vector<std::pair<int,int>>, int> Goban::getGroupInfo(int i, int j) const
{
    if (!inBoard(i,j) || get(i,j) == 0) {
        return {}; // 返回一个空的QPair
    }

    int color = get(i,j);
    std::vector<std::pair<int,int>> groupStones;
    std::set<std::pair<int,int>> libertiesSet; // 使用 set 存储气, 可以自动去重
    std::vector<char> visited(m_board.size(), 0);
    std::queue<std::pair<int,int>> q;

    q.push({i,j});
    visited[idx(i,j)] = 1;

    while (!q.empty()) {
        auto cur = q.front();
        q.pop();
        groupStones.push_back(cur);

        for (auto nb : neighbors(cur.first, cur.second)) {
            int val = get(nb.first, nb.second);
            if (val == 0) { // 这是一个气
                libertiesSet.insert(nb);
            } else if (val == color) {
                if (!visited[idx(nb.first, nb.second)]) {
                    visited[idx(nb.first, nb.second)] = 1;
                    q.push(nb);
                }
            }
        }
    }

    return qMakePair(groupStones, libertiesSet.size());
}

const std::vector<std::pair<std::pair<int, int>, int>>& Goban::getMoveHistory() const
{
    return m_moveHistory;
}
