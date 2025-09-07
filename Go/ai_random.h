#ifndef AI_RANDOM_H
#define AI_RANDOM_H

#include "goban.h"
#include <utility>

class AIRandom
{
public:
    static std::pair<int,int> chooseMove(const Goban &board, int color);
};

#endif
