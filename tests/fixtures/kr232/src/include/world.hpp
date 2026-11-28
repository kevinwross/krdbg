#pragma once

#include "player.hpp"

class World {
public:
    int tick(int delta);
    Player player;
};
