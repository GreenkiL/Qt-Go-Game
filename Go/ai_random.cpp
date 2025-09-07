#include "ai_random.h"
#include <QRandomGenerator>

std::pair<int,int> AIRandom::chooseMove(const Goban &board, int color)
{
    auto moves = board.legalMoves(color);
    if (moves.empty()) {
        return {-1,-1};
    }
    int idx = QRandomGenerator::global()->bounded((int)moves.size());
    return moves[idx];
}
