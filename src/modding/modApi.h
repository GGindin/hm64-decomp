#ifndef _MOD_API_H_
#define _MOD_API_H_

#include "common.h"

#define MOD_API_DEFAULT_GAME_TIME_SPEED 10

typedef struct {
    u16 speed;
} ModTimeConfigApi;

typedef struct {
    ModTimeConfigApi time;
} ModConfigApi;

typedef struct {
    /* Keep runtime groups non-empty until a real time runtime field exists. */
    u8 reserved;
} ModTimeRuntimeApi;

typedef struct {
    ModTimeRuntimeApi time;
} ModRuntimeApi;

typedef struct {
    ModConfigApi config;
    ModRuntimeApi runtime;
} ModApi;

extern ModApi api;

/* Stable mod configuration: reset once before generated Python settings apply. */
extern void resetModConfigDefaults(void);

/* Runtime reset scopes: add per-lifetime state here instead of resetting all API data. */
extern void resetModRuntimeForNewGame(void);
extern void resetModRuntimeForNewDay(void);
extern void resetModRuntimeForMapLoad(void);
extern u16 getGameTimeSpeed(void);

#endif
