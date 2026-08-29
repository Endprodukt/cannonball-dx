/***************************************************************************
    CannonBall DX experimental two-player multiplayer prototype.

    This first step deliberately keeps both game instances authoritative for
    their own physics. It exchanges only the state needed to draw the remote
    Ferrari in the local road scene.
***************************************************************************/

#pragma once

class Multiplayer
{
public:
    Multiplayer();
    ~Multiplayer();

    // Called once per game-logic tick. Handles lazy socket setup, state
    // exchange and drawing of the remote Ferrari.
    void tick();

    bool connected() const;

private:
    struct Impl;
    Impl* impl;
};

extern Multiplayer multiplayer;
