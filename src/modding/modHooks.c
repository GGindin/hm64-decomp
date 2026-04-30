#include "common.h"

#include "modding/modApi.h"
#include "modding/modHooks.h"
#include "modding/modMenu.h"

void applyGeneratedModSettings(void);

void initializeMods(void) {
#if MODLOADER_ENABLE_PATCHES
    resetModConfigDefaults();
    resetModRuntimeForNewGame();
    applyGeneratedModSettings();
#endif
}

void resetModsForNewDay(void) {
#if MODLOADER_ENABLE_PATCHES
    resetModRuntimeForNewDay();
    applyModMenuPendingChangesForNewDay();
#endif
}

void resetModsForMapLoad(void) {
#if MODLOADER_ENABLE_PATCHES
    resetModRuntimeForMapLoad();
    applyModMenuPendingChangesForMapLoad();
#endif
}
