#include "common.h"

#include "modding/modApi.h"

ModApi api = {
    {
        {
            MOD_API_DEFAULT_GAME_TIME_SPEED
        }
    },
    {
        {
            0
        }
    }
};

void resetModConfigDefaults(void) {
    api.config.time.speed = MOD_API_DEFAULT_GAME_TIME_SPEED;
}

void resetModRuntimeForNewGame(void) {
    api.runtime.time.reserved = 0;
}

void resetModRuntimeForNewDay(void) {
}

void resetModRuntimeForMapLoad(void) {
}

u16 getGameTimeSpeed(void) {
    return api.config.time.speed;
}
