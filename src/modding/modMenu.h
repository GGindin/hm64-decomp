#ifndef _MOD_MENU_H_
#define _MOD_MENU_H_

#include "common.h"

extern bool checkModMenuOpen(void);
extern bool checkModMenuOpenShortcut(void);
extern void openModMenu(void);
extern void closeModMenu(void);
extern void updateModMenu(void);
extern void applyModMenuPendingChangesForNewDay(void);
extern void applyModMenuPendingChangesForMapLoad(void);

#endif
