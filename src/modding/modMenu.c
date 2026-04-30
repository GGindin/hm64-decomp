#include "common.h"
#include "macros.h"

#include "modding/modApi.h"
#include "modding/modMenu.h"

#include "system/audio.h"
#include "system/controller.h"
#include "system/globalSprites.h"
#include "system/map.h"
#include "system/mapController.h"
#include "system/message.h"
#include "system/sprite.h"

#include "game/game.h"
#include "game/items.h"
#include "game/level.h"
#include "game/npc.h"
#include "game/player.h"
#include "game/time.h"
#include "game/weather.h"

#include "assetIndices/sprites.h"

#include "buffers/buffers.h"

#include "mainLoop.h"

#if MODLOADER_ENABLE_PATCHES

#define MOD_MENU_MESSAGE_BOX_INDEX 5
#define MOD_MENU_DIALOGUE_WINDOW_INDEX 1
#define MOD_MENU_WINDOW_SPRITE 0x81
#define MOD_MENU_TEXT_BUFFER_SIZE 192
#define MOD_MENU_TEXT_LINE_WIDTH 17
#define MOD_MENU_TEXT_ROWS 4
#define MOD_MENU_TEXT_ENCODING_OFFSET 0x0B
#define MOD_MENU_VIEW_SPACE_X 0.0f
#define MOD_MENU_VIEW_SPACE_Y 72.0f
#define MOD_MENU_VIEW_SPACE_Z 352.0f
#define MOD_MENU_WINDOW_SCALE_X 0.42f
#define MOD_MENU_WINDOW_SCALE_Y 0.50f
#define MOD_MENU_FAST_STEP_BUTTON BUTTON_A

#define MOD_MENU_INPUT_UP (BUTTON_D_UP | BUTTON_STICK_NORTH | BUTTON_STICK_NORTHEAST | BUTTON_STICK_NORTHWEST)
#define MOD_MENU_INPUT_DOWN (BUTTON_D_DOWN | BUTTON_STICK_SOUTH | BUTTON_STICK_SOUTHEAST | BUTTON_STICK_SOUTHWEST)
#define MOD_MENU_INPUT_LEFT (BUTTON_D_LEFT | BUTTON_STICK_WEST | BUTTON_STICK_NORTHWEST | BUTTON_STICK_SOUTHWEST)
#define MOD_MENU_INPUT_RIGHT (BUTTON_D_RIGHT | BUTTON_STICK_EAST | BUTTON_STICK_NORTHEAST | BUTTON_STICK_SOUTHEAST)

#define MOD_MENU_VALUE_U8 0
#define MOD_MENU_VALUE_U16 1
#define MOD_MENU_VALUE_U32 2

#define MOD_MENU_APPLY_LIVE 0
#define MOD_MENU_APPLY_ON_MAP_LOAD 1
#define MOD_MENU_APPLY_ON_NEW_DAY 2
#define MOD_MENU_APPLY_MANUAL 3

#define MOD_MENU_CATEGORY_WORLD 0
#define MOD_MENU_CATEGORY_TIME 1
#define MOD_MENU_CATEGORY_PLAYER 2
#define MOD_MENU_CATEGORY_FARM 3
#define MOD_MENU_CATEGORY_NPCS 4

#define MOD_MENU_WORLD_FIRST 0
#define MOD_MENU_WORLD_COUNT 2
#define MOD_MENU_TIME_FIRST (MOD_MENU_WORLD_FIRST + MOD_MENU_WORLD_COUNT)
#define MOD_MENU_TIME_COUNT 6
#define MOD_MENU_PLAYER_FIRST (MOD_MENU_TIME_FIRST + MOD_MENU_TIME_COUNT)
#define MOD_MENU_PLAYER_COUNT 5
#define MOD_MENU_FARM_FIRST (MOD_MENU_PLAYER_FIRST + MOD_MENU_PLAYER_COUNT)
#define MOD_MENU_FARM_COUNT 2
#define MOD_MENU_NPCS_FIRST (MOD_MENU_FARM_FIRST + MOD_MENU_FARM_COUNT)
#define MOD_MENU_NPCS_COUNT (ENTOMOLOGIST + 1)

#define MOD_MENU_NPC_AFFECTION_STEP 5
#define MOD_MENU_NPC_AFFECTION_FAST_STEP 25
#define MOD_MENU_BINDING(labelText, category, type, apply, target, minimum, maximum, baseStep, highStep, resetValue, valueLabel, changeCallback) \
    { labelText, category, type, apply, target, minimum, maximum, baseStep, highStep, resetValue, valueLabel, changeCallback, FALSE, 0 }
#define MOD_MENU_NPC_AFFECTION_BINDING(labelText, npcIndex) \
    MOD_MENU_BINDING(labelText, MOD_MENU_CATEGORY_NPCS, MOD_MENU_VALUE_U8, MOD_MENU_APPLY_LIVE, &npcAffection[npcIndex], 0, MAX_AFFECTION, MOD_MENU_NPC_AFFECTION_STEP, MOD_MENU_NPC_AFFECTION_FAST_STEP, 0, NULL, NULL)

typedef struct {
    u8 open;
    u8 categoryIndex;
    u8 itemIndex;
} ModMenuState;

typedef struct {
    const char *label;
    u8 firstBindingIndex;
    u8 bindingCount;
} ModMenuCategory;

typedef struct {
    const char *label;
    u8 categoryIndex;
    u8 valueType;
    u8 applyMode;
    void *valuePtr;
    u32 minValue;
    u32 maxValue;
    u32 step;
    u32 fastStep;
    u32 defaultValue;
    const char *(*getValueLabel)(u32 value);
    void (*onChange)(void);
    u8 pending;
    u32 pendingValue;
} ModMenuBinding;

static u8 modMenuTextBuffer[MOD_MENU_TEXT_BUFFER_SIZE] ALIGNED(8);
static u8 modMenuTextBufferIndex;
static u8 modMenuTextGroupCount;
static u8 modMenuLineCount;
static u8 modMenuMaxLineLength;

static ModMenuState modMenuState = { 0, 0, 0 };

static const char *getWeatherValueLabel(u32 value);
static const char *getSeasonValueLabel(u32 value);
static const char *getDayOfWeekValueLabel(u32 value);
static void applyWeatherChange(void);
static void applyTimeChange(void);
static void applyCalendarChange(void);
static void loadModMenuWindowSprite(void);
static void refreshModMenuText(void);

static ModMenuCategory modMenuCategories[] = {
    { "WORLD", MOD_MENU_WORLD_FIRST, MOD_MENU_WORLD_COUNT },
    { "TIME", MOD_MENU_TIME_FIRST, MOD_MENU_TIME_COUNT },
    { "PLAYER", MOD_MENU_PLAYER_FIRST, MOD_MENU_PLAYER_COUNT },
    { "FARM", MOD_MENU_FARM_FIRST, MOD_MENU_FARM_COUNT },
    { "NPCS", MOD_MENU_NPCS_FIRST, MOD_MENU_NPCS_COUNT }
};

static ModMenuBinding modMenuBindings[] = {
    MOD_MENU_BINDING("WEATHER", MOD_MENU_CATEGORY_WORLD, MOD_MENU_VALUE_U8, MOD_MENU_APPLY_LIVE, &gWeather, SUNNY, TYPHOON, 1, 1, SUNNY, getWeatherValueLabel, applyWeatherChange),
    MOD_MENU_BINDING("FORECAST", MOD_MENU_CATEGORY_WORLD, MOD_MENU_VALUE_U8, MOD_MENU_APPLY_MANUAL, &gForecast, SUNNY, TYPHOON, 1, 1, SUNNY, getWeatherValueLabel, NULL),

    MOD_MENU_BINDING("CLOCK SPEED", MOD_MENU_CATEGORY_TIME, MOD_MENU_VALUE_U16, MOD_MENU_APPLY_LIVE, &api.config.time.speed, 0, 999, 1, 10, MOD_API_DEFAULT_GAME_TIME_SPEED, NULL, NULL),
    MOD_MENU_BINDING("HOUR", MOD_MENU_CATEGORY_TIME, MOD_MENU_VALUE_U8, MOD_MENU_APPLY_LIVE, &gHour, 0, 23, 1, 6, 6, NULL, applyTimeChange),
    MOD_MENU_BINDING("MINUTES", MOD_MENU_CATEGORY_TIME, MOD_MENU_VALUE_U8, MOD_MENU_APPLY_LIVE, &gMinutes, 0, 59, 10, 30, 0, NULL, NULL),
    MOD_MENU_BINDING("DAY", MOD_MENU_CATEGORY_TIME, MOD_MENU_VALUE_U8, MOD_MENU_APPLY_ON_NEW_DAY, &gDayOfMonth, 1, 30, 1, 7, 1, NULL, applyCalendarChange),
    MOD_MENU_BINDING("WEEKDAY", MOD_MENU_CATEGORY_TIME, MOD_MENU_VALUE_U8, MOD_MENU_APPLY_ON_NEW_DAY, &gDayOfWeek, SUNDAY, SATURDAY, 1, 1, SUNDAY, getDayOfWeekValueLabel, NULL),
    MOD_MENU_BINDING("SEASON", MOD_MENU_CATEGORY_TIME, MOD_MENU_VALUE_U8, MOD_MENU_APPLY_ON_MAP_LOAD, &gSeason, SPRING, WINTER, 1, 1, SPRING, getSeasonValueLabel, applyCalendarChange),

    MOD_MENU_BINDING("GOLD", MOD_MENU_CATEGORY_PLAYER, MOD_MENU_VALUE_U32, MOD_MENU_APPLY_LIVE, &gGold, 0, MAX_GOLD, 1000, 10000, 0, NULL, NULL),
    MOD_MENU_BINDING("STAMINA", MOD_MENU_CATEGORY_PLAYER, MOD_MENU_VALUE_U8, MOD_MENU_APPLY_LIVE, &gPlayer.currentStamina, 0, MAX_STAMINA, 5, 25, MAX_STAMINA, NULL, NULL),
    MOD_MENU_BINDING("MAX STAMINA", MOD_MENU_CATEGORY_PLAYER, MOD_MENU_VALUE_U8, MOD_MENU_APPLY_LIVE, &gMaximumStamina, 1, MAX_STAMINA, 5, 25, MAX_STAMINA, NULL, NULL),
    MOD_MENU_BINDING("FATIGUE", MOD_MENU_CATEGORY_PLAYER, MOD_MENU_VALUE_U8, MOD_MENU_APPLY_LIVE, &gPlayer.fatigueCounter, 0, MAX_FATIGUE_POINTS, 5, 25, 0, NULL, NULL),
    MOD_MENU_BINDING("HAPPINESS", MOD_MENU_CATEGORY_PLAYER, MOD_MENU_VALUE_U8, MOD_MENU_APPLY_LIVE, &gHappiness, 0, MAX_HAPPINESS, 5, 25, 0, NULL, NULL),

    MOD_MENU_BINDING("LUMBER", MOD_MENU_CATEGORY_FARM, MOD_MENU_VALUE_U16, MOD_MENU_APPLY_LIVE, &gLumber, 0, MAX_LUMBER, 10, 100, 0, NULL, NULL),
    MOD_MENU_BINDING("FODDER", MOD_MENU_CATEGORY_FARM, MOD_MENU_VALUE_U16, MOD_MENU_APPLY_LIVE, &fodderQuantity, 0, MAX_FODDER, 10, 100, 0, NULL, NULL),

    MOD_MENU_NPC_AFFECTION_BINDING("MARIA", MARIA),
    MOD_MENU_NPC_AFFECTION_BINDING("POPURI", POPURI),
    MOD_MENU_NPC_AFFECTION_BINDING("ELLI", ELLI),
    MOD_MENU_NPC_AFFECTION_BINDING("ANN", ANN),
    MOD_MENU_NPC_AFFECTION_BINDING("KAREN", KAREN),
    MOD_MENU_NPC_AFFECTION_BINDING("BABY", BABY),
    MOD_MENU_NPC_AFFECTION_BINDING("HARRIS", HARRIS),
    MOD_MENU_NPC_AFFECTION_BINDING("GRAY", GRAY),
    MOD_MENU_NPC_AFFECTION_BINDING("JEFF", JEFF),
    MOD_MENU_NPC_AFFECTION_BINDING("CLIFF", CLIFF),
    MOD_MENU_NPC_AFFECTION_BINDING("KAI", KAI),
    MOD_MENU_NPC_AFFECTION_BINDING("MAYOR", MAYOR),
    MOD_MENU_NPC_AFFECTION_BINDING("MAYOR WIFE", MAYOR_WIFE),
    MOD_MENU_NPC_AFFECTION_BINDING("LILLIA", LILLIA),
    MOD_MENU_NPC_AFFECTION_BINDING("BASIL", BASIL),
    MOD_MENU_NPC_AFFECTION_BINDING("ELLEN", ELLEN),
    MOD_MENU_NPC_AFFECTION_BINDING("DOUG", DOUG),
    MOD_MENU_NPC_AFFECTION_BINDING("GOTZ", GOTZ),
    MOD_MENU_NPC_AFFECTION_BINDING("SASHA", SASHA),
    MOD_MENU_NPC_AFFECTION_BINDING("POTION DEALER", POTION_SHOP_DEALER),
    MOD_MENU_NPC_AFFECTION_BINDING("KENT", KENT),
    MOD_MENU_NPC_AFFECTION_BINDING("STU", STU),
    MOD_MENU_NPC_AFFECTION_BINDING("MIDWIFE", MIDWIFE),
    MOD_MENU_NPC_AFFECTION_BINDING("MAY", MAY),
    MOD_MENU_NPC_AFFECTION_BINDING("RICK", RICK),
    MOD_MENU_NPC_AFFECTION_BINDING("PASTOR", PASTOR),
    MOD_MENU_NPC_AFFECTION_BINDING("SHIPPER", SHIPPER),
    MOD_MENU_NPC_AFFECTION_BINDING("SAIBARA", SAIBARA),
    MOD_MENU_NPC_AFFECTION_BINDING("DUKE", DUKE),
    MOD_MENU_NPC_AFFECTION_BINDING("GREG", GREG),
    MOD_MENU_NPC_AFFECTION_BINDING("CARPENTER 1", CARPENTER_1),
    MOD_MENU_NPC_AFFECTION_BINDING("CARPENTER 2", CARPENTER_2),
    MOD_MENU_NPC_AFFECTION_BINDING("MASTER CARP", MASTER_CARPENTER),
    MOD_MENU_NPC_AFFECTION_BINDING("SPRITE 1", HARVEST_SPRITE_1),
    MOD_MENU_NPC_AFFECTION_BINDING("SPRITE 2", HARVEST_SPRITE_2),
    MOD_MENU_NPC_AFFECTION_BINDING("SPRITE 3", HARVEST_SPRITE_3),
    MOD_MENU_NPC_AFFECTION_BINDING("SYDNEY", SYDNEY),
    MOD_MENU_NPC_AFFECTION_BINDING("BARLEY", BARLEY),
    MOD_MENU_NPC_AFFECTION_BINDING("MRS MANA", MRS_MANA),
    MOD_MENU_NPC_AFFECTION_BINDING("JOHN", JOHN),
    MOD_MENU_NPC_AFFECTION_BINDING("GOURMET JUDGE", GOURMET_JUDGE),
    MOD_MENU_NPC_AFFECTION_BINDING("MARIA BABY", MARIA_HARRIS_BABY),
    MOD_MENU_NPC_AFFECTION_BINDING("POPURI BABY", POPURI_GRAY_BABY),
    MOD_MENU_NPC_AFFECTION_BINDING("ELLI BABY", ELLI_JEFF_BABY),
    MOD_MENU_NPC_AFFECTION_BINDING("ANN BABY", ANN_CLIFF_BABY),
    MOD_MENU_NPC_AFFECTION_BINDING("KAREN BABY", KAREN_KAI_BABY),
    MOD_MENU_NPC_AFFECTION_BINDING("ENTOMOLOGIST", ENTOMOLOGIST)
};

static const u8 modMenuCategoryCount = sizeof(modMenuCategories) / sizeof(modMenuCategories[0]);
static const u8 modMenuBindingCount = sizeof(modMenuBindings) / sizeof(modMenuBindings[0]);

static u8 getModMenuBindingIndex(void) {
    return modMenuCategories[modMenuState.categoryIndex].firstBindingIndex + modMenuState.itemIndex;
}

static ModMenuBinding *getSelectedModMenuBinding(void) {
    return &modMenuBindings[getModMenuBindingIndex()];
}

static u32 getModMenuBindingTargetValue(ModMenuBinding *binding) {
    u32 value;

    value = 0;

    switch (binding->valueType) {
        case MOD_MENU_VALUE_U8:
            value = *(u8*)binding->valuePtr;
            break;
        case MOD_MENU_VALUE_U16:
            value = *(u16*)binding->valuePtr;
            break;
        case MOD_MENU_VALUE_U32:
            value = *(u32*)binding->valuePtr;
            break;
        default:
            break;
    }

    return value;
}

static u32 getModMenuBindingValue(ModMenuBinding *binding) {
    if (binding->pending) {
        return binding->pendingValue;
    }

    return getModMenuBindingTargetValue(binding);
}

static u32 clampModMenuBindingValue(ModMenuBinding *binding, u32 value) {

    if (value < binding->minValue) {
        value = binding->minValue;
    }

    if (value > binding->maxValue) {
        value = binding->maxValue;
    }

    return value;
}

static void writeModMenuBindingTargetValue(ModMenuBinding *binding, u32 value) {

    value = clampModMenuBindingValue(binding, value);

    switch (binding->valueType) {
        case MOD_MENU_VALUE_U8:
            *(u8*)binding->valuePtr = value;
            break;
        case MOD_MENU_VALUE_U16:
            *(u16*)binding->valuePtr = value;
            break;
        case MOD_MENU_VALUE_U32:
            *(u32*)binding->valuePtr = value;
            break;
        default:
            break;
    }

}

static void applyModMenuBindingValue(ModMenuBinding *binding, u32 value) {

    writeModMenuBindingTargetValue(binding, value);

    if (binding->onChange != NULL) {
        binding->onChange();
    }

}

static void applyPendingModMenuBindingValue(ModMenuBinding *binding) {

    if (binding->pending) {
        applyModMenuBindingValue(binding, binding->pendingValue);
        binding->pending = FALSE;
    }

}

static void setModMenuBindingValue(ModMenuBinding *binding, u32 value) {

    value = clampModMenuBindingValue(binding, value);

    if (binding->applyMode == MOD_MENU_APPLY_LIVE) {
        binding->pending = FALSE;
        applyModMenuBindingValue(binding, value);
    } else {
        binding->pendingValue = value;
        binding->pending = TRUE;
    }

}

static u32 getModMenuBindingStep(ModMenuBinding *binding) {

    u32 step;

    if (checkButtonHeld(CONTROLLER_1, MOD_MENU_FAST_STEP_BUTTON)) {
        step = binding->fastStep;
    } else {
        step = binding->step;
    }

    if (step == 0) {
        step = 1;
    }

    return step;

}

static void adjustModMenuBindingValue(ModMenuBinding *binding, s8 direction) {

    u32 value;
    u32 step;

    value = getModMenuBindingValue(binding);
    step = getModMenuBindingStep(binding);

    if (direction > 0) {
        if (value + step > binding->maxValue || value + step < value) {
            value = binding->minValue;
        } else {
            value += step;
        }
    } else {
        if (value < binding->minValue + step || binding->minValue + step < binding->minValue) {
            value = binding->maxValue;
        } else {
            value -= step;
        }
    }

    setModMenuBindingValue(binding, value);

}

static void resetModMenuBindingValue(ModMenuBinding *binding) {
    setModMenuBindingValue(binding, binding->defaultValue);
}

static const char *getWeatherValueLabel(u32 value) {

    const char *label;

    switch (value) {
        case SUNNY:
            label = "SUNNY";
            break;
        case RAIN:
            label = "RAIN";
            break;
        case SNOW:
            label = "SNOW";
            break;
        case 4:
            label = "RAIN 2";
            break;
        case TYPHOON:
            label = "TYPHOON";
            break;
        default:
            label = "UNKNOWN";
            break;
    }

    return label;

}

static const char *getSeasonValueLabel(u32 value) {

    const char *label;

    switch (value) {
        case SPRING:
            label = "SPRING";
            break;
        case SUMMER:
            label = "SUMMER";
            break;
        case AUTUMN:
            label = "AUTUMN";
            break;
        case WINTER:
            label = "WINTER";
            break;
        default:
            label = "UNKNOWN";
            break;
    }

    return label;

}

static const char *getDayOfWeekValueLabel(u32 value) {

    const char *label;

    switch (value) {
        case SUNDAY:
            label = "SUN";
            break;
        case MONDAY:
            label = "MON";
            break;
        case TUESDAY:
            label = "TUE";
            break;
        case WEDNESDAY:
            label = "WED";
            break;
        case THURSDAY:
            label = "THU";
            break;
        case FRIDAY:
            label = "FRI";
            break;
        case SATURDAY:
            label = "SAT";
            break;
        default:
            label = "UNKNOWN";
            break;
    }

    return label;

}

static void clearWeatherSpritesForModMenu(void) {

    u8 i;

    if (!(mainMap[MAIN_MAP_INDEX].mapState.flags & MAP_ACTIVE)) {
        return;
    }

    for (i = 0; i < MAX_WEATHER_SPRITES; i++) {
        if (mainMap[MAIN_MAP_INDEX].weatherSprites[i].flags & MAP_WEATHER_SPRITE_ACTIVE) {
            deactivateSprite(mainMap[MAIN_MAP_INDEX].weatherSprites[i].spriteIndex);
        }

        mainMap[MAIN_MAP_INDEX].weatherSprites[i].flags = 0;
    }

}

static void applyWeatherChange(void) {

    clearWeatherSpritesForModMenu();

    if (getLevelFlags(gBaseMapIndex) & LEVEL_OUTDOORS) {
        setWeatherSprites();
    }

    setLevelLighting(0, NO_OP);

}

static void applyTimeChange(void) {
    setLevelLighting(0, NO_OP);
}

static void applyCalendarChange(void) {
    gSeasonTomorrow = gSeason;

    if ((gDayOfMonth + 1) >= 31) {
        gSeasonTomorrow = gSeason + 1;
    }

    if (gSeasonTomorrow >= 5) {
        gSeasonTomorrow = SPRING;
    }

    setSeasonName();
}

static void loadModMenuWindowSprite(void) {

    u16 spriteIndex;

    spriteIndex = MOD_MENU_WINDOW_SPRITE;

    dmaSprite(
        spriteIndex,
        dialogueWindows[MOD_MENU_DIALOGUE_WINDOW_INDEX].romTextureStart,
        dialogueWindows[MOD_MENU_DIALOGUE_WINDOW_INDEX].romTextureEnd,
        dialogueWindows[MOD_MENU_DIALOGUE_WINDOW_INDEX].romIndexStart,
        dialogueWindows[MOD_MENU_DIALOGUE_WINDOW_INDEX].romIndexEnd,
        NULL,
        NULL,
        dialogueWindows[MOD_MENU_DIALOGUE_WINDOW_INDEX].vaddrTexture,
        NULL,
        dialogueWindows[MOD_MENU_DIALOGUE_WINDOW_INDEX].vaddrPalette,
        dialogueWindows[MOD_MENU_DIALOGUE_WINDOW_INDEX].vaddrAnimationFrameMetadata,
        (u8*)dialogueWindows[MOD_MENU_DIALOGUE_WINDOW_INDEX].vaddrTextureToPaletteLookup,
        (u32*)dialogueWindows[MOD_MENU_DIALOGUE_WINDOW_INDEX].unk_20,
        0,
        0
    );

    setBilinearFiltering(spriteIndex, TRUE);
    setSpriteAnchorAlignment(spriteIndex, SPRITE_ANCHOR_CENTER, SPRITE_ANCHOR_CENTER);
    setSpriteBlendMode(spriteIndex, SPRITE_BLEND_ALPHA_DECAL);
    setSpriteColor(spriteIndex, 255, 255, 255, 224);
    setSpriteScale(spriteIndex, MOD_MENU_TEXT_LINE_WIDTH * MOD_MENU_WINDOW_SCALE_X, MOD_MENU_TEXT_ROWS * MOD_MENU_WINDOW_SCALE_Y, 1.0f);
    startSpriteAnimation(
        spriteIndex,
        dialogueWindows[MOD_MENU_DIALOGUE_WINDOW_INDEX].spriteOffset,
        dialogueWindows[MOD_MENU_DIALOGUE_WINDOW_INDEX].flag
    );
    setSpriteViewSpacePosition(
        spriteIndex,
        messageBoxes[MOD_MENU_MESSAGE_BOX_INDEX].viewSpacePosition.x,
        messageBoxes[MOD_MENU_MESSAGE_BOX_INDEX].viewSpacePosition.y,
        messageBoxes[MOD_MENU_MESSAGE_BOX_INDEX].viewSpacePosition.z - 2.0f
    );

}

static void beginModMenuText(void) {
    modMenuTextBufferIndex = 0;
    modMenuTextGroupCount = 8;
    modMenuLineCount = 1;
    modMenuMaxLineLength = 0;
}

static void appendModMenuTextCode(u8 code) {

    if (modMenuTextBufferIndex >= MOD_MENU_TEXT_BUFFER_SIZE - 2) {
        return;
    }

    if (modMenuTextGroupCount == 8) {
        modMenuTextBuffer[modMenuTextBufferIndex++] = 0;
        modMenuTextGroupCount = 0;
    }

    modMenuTextBuffer[modMenuTextBufferIndex++] = code;
    modMenuTextGroupCount++;

}

static u8 getModMenuTextCode(char value) {

    if (value >= 'A' && value <= 'Z') {
        return char_A + (value - 'A') + MOD_MENU_TEXT_ENCODING_OFFSET;
    }

    if (value >= 'a' && value <= 'z') {
        return char_a + (value - 'a') + MOD_MENU_TEXT_ENCODING_OFFSET;
    }

    if (value >= '1' && value <= '9') {
        return char_1 + (value - '1') + MOD_MENU_TEXT_ENCODING_OFFSET;
    }

    if (value == '0') {
        return char_0 + MOD_MENU_TEXT_ENCODING_OFFSET;
    }

    switch (value) {
        case '-':
            return char_DASH + MOD_MENU_TEXT_ENCODING_OFFSET;
        case '.':
            return char_PERIOD + MOD_MENU_TEXT_ENCODING_OFFSET;
        case '/':
            return char_SLASH + MOD_MENU_TEXT_ENCODING_OFFSET;
        case '?':
            return char_QUESTION_MARK + MOD_MENU_TEXT_ENCODING_OFFSET;
        case '!':
            return char_EXCLAMATION_MARK + MOD_MENU_TEXT_ENCODING_OFFSET;
        default:
            break;
    }

    return char_SPACE + MOD_MENU_TEXT_ENCODING_OFFSET;

}

static void appendModMenuFontCode(u8 code, u8 *lineLength) {
    if (*lineLength >= MOD_MENU_TEXT_LINE_WIDTH) {
        return;
    }

    appendModMenuTextCode(code + MOD_MENU_TEXT_ENCODING_OFFSET);
    (*lineLength)++;
}

static void appendModMenuAscii(const char *text, u8 *lineLength) {

    while (*text != '\0' && *lineLength < MOD_MENU_TEXT_LINE_WIDTH) {
        appendModMenuTextCode(getModMenuTextCode(*text));
        (*lineLength)++;
        text++;
    }

}

static void appendModMenuNumber(u32 value, u8 *lineLength) {

    u32 divisor;
    u8 digit;
    bool started;

    divisor = 1000000000;
    started = FALSE;

    while (divisor != 0 && *lineLength < MOD_MENU_TEXT_LINE_WIDTH) {
        digit = value / divisor;

        if (digit != 0 || started || divisor == 1) {
            appendModMenuTextCode(getModMenuTextCode('0' + digit));
            (*lineLength)++;
            started = TRUE;
        }

        value = value % divisor;
        divisor = divisor / 10;
    }

}

static void padModMenuTextLine(u8 *lineLength) {
    while (*lineLength < MOD_MENU_TEXT_LINE_WIDTH) {
        appendModMenuTextCode(getModMenuTextCode(' '));
        (*lineLength)++;
    }
}

static void finishModMenuTextLine(u8 lineLength) {

    padModMenuTextLine(&lineLength);

    if (modMenuMaxLineLength < lineLength) {
        modMenuMaxLineLength = lineLength;
    }

    appendModMenuTextCode(CHARACTER_CONTROL_LINEBREAK);
    modMenuLineCount++;

}

static void finishModMenuText(u8 lineLength) {
    padModMenuTextLine(&lineLength);
    appendModMenuTextCode(CHARACTER_CONTROL_TEXT_END);

    if (modMenuMaxLineLength < lineLength) {
        modMenuMaxLineLength = lineLength;
    }
}

static void writeModMenuHeaderLine(void) {

    u8 lineLength;
    ModMenuCategory *category;

    category = &modMenuCategories[modMenuState.categoryIndex];

    lineLength = 0;
    appendModMenuAscii("MOD ", &lineLength);
    appendModMenuAscii(category->label, &lineLength);
    appendModMenuAscii(" ", &lineLength);
    appendModMenuNumber(modMenuState.itemIndex + 1, &lineLength);
    appendModMenuAscii("/", &lineLength);
    appendModMenuNumber(category->bindingCount, &lineLength);
    finishModMenuTextLine(lineLength);

}

static void writeModMenuItemLine(ModMenuBinding *binding) {

    u8 lineLength;

    lineLength = 0;
    appendModMenuFontCode(char_STAR_1, &lineLength);
    appendModMenuAscii(" ", &lineLength);
    appendModMenuAscii(binding->label, &lineLength);
    finishModMenuTextLine(lineLength);

}

static const char *getModMenuValuePrefix(ModMenuBinding *binding) {

    const char *label;

    if (binding->pending) {
        return "PEND ";
    }

    switch (binding->applyMode) {
        case MOD_MENU_APPLY_ON_MAP_LOAD:
            label = "MAP ";
            break;
        case MOD_MENU_APPLY_ON_NEW_DAY:
            label = "DAY ";
            break;
        case MOD_MENU_APPLY_MANUAL:
            label = "MAN ";
            break;
        default:
            label = "LIVE ";
            break;
    }

    return label;

}

static void writeModMenuValueLine(ModMenuBinding *binding) {

    u8 lineLength;
    u32 value;

    value = getModMenuBindingValue(binding);
    lineLength = 0;

    appendModMenuAscii(getModMenuValuePrefix(binding), &lineLength);
    appendModMenuNumber(value, &lineLength);

    if (binding->getValueLabel != NULL) {
        appendModMenuAscii(" ", &lineLength);
        appendModMenuAscii(binding->getValueLabel(value), &lineLength);
    }

    finishModMenuTextLine(lineLength);

}

static void writeModMenuHelpLine(ModMenuBinding *binding) {

    u8 lineLength;

    lineLength = 0;

    if (binding->pending && binding->applyMode == MOD_MENU_APPLY_ON_MAP_LOAD) {
        appendModMenuAscii("APPLY MAP LOAD", &lineLength);
    } else if (binding->pending && binding->applyMode == MOD_MENU_APPLY_ON_NEW_DAY) {
        appendModMenuAscii("APPLY NEW DAY", &lineLength);
    } else if (binding->applyMode == MOD_MENU_APPLY_MANUAL) {
        appendModMenuAscii("A APPLY Z RST", &lineLength);
    } else {
        appendModMenuAscii("Z RST A FAST", &lineLength);
    }

    finishModMenuText(lineLength);

}

static void refreshModMenuText(void) {

    ModMenuBinding *binding;

    binding = getSelectedModMenuBinding();

    beginModMenuText();
    writeModMenuHeaderLine();
    writeModMenuItemLine(binding);
    writeModMenuValueLine(binding);
    writeModMenuHelpLine(binding);

    messageBoxes[MOD_MENU_MESSAGE_BOX_INDEX].currentCompressionControlByte = 0;
    messageBoxes[MOD_MENU_MESSAGE_BOX_INDEX].compressionBitIndex = 0;
    messageBoxes[MOD_MENU_MESSAGE_BOX_INDEX].scrollCount = 0;
    messageBoxes[MOD_MENU_MESSAGE_BOX_INDEX].currentCharCountOnLine = 0;
    messageBoxes[MOD_MENU_MESSAGE_BOX_INDEX].currentLineBeingPrinted = 0;
    messageBoxes[MOD_MENU_MESSAGE_BOX_INDEX].currentCharPtr = modMenuTextBuffer;
    messageBoxes[MOD_MENU_MESSAGE_BOX_INDEX].totalLinesToPrint = modMenuLineCount - 1;
    messageBoxes[MOD_MENU_MESSAGE_BOX_INDEX].totalLines = modMenuLineCount;
    messageBoxes[MOD_MENU_MESSAGE_BOX_INDEX].maxCharsPerLine = modMenuMaxLineLength;
    messageBoxes[MOD_MENU_MESSAGE_BOX_INDEX].flags &= ~(MESSAGE_BOX_TEXT_COMPLETE | MESSAGE_BOX_TEXT_END_REACHED | MESSAGE_BOX_BUTTON_PRESSED | MESSAGE_BOX_WAITING_WITH_ICON | MESSAGE_BOX_PROCESSING_GAME_VARIABLE | 0x4000);
    messageBoxes[MOD_MENU_MESSAGE_BOX_INDEX].flags |= MESSAGE_BOX_INITIALIZED | MESSAGE_BOX_MODE_NO_INPUT;

    setSpriteColor(CURSOR_HAND, 255, 255, 255, 255);
    setSpriteViewSpacePosition(CURSOR_HAND, -136.0f, 76.0f, 384.0f);
    startSpriteAnimation(CURSOR_HAND, 3, 0);

}

static void initializeModMenuMessageBox(void) {

    if (messageBoxes[MOD_MENU_MESSAGE_BOX_INDEX].flags & MESSAGE_BOX_ACTIVE) {
        deactivateMessageBox(MOD_MENU_MESSAGE_BOX_INDEX);
    }

    initializeEmptyMessageBox(MOD_MENU_MESSAGE_BOX_INDEX, modMenuTextBuffer);
    setMessageBoxViewSpacePosition(MOD_MENU_MESSAGE_BOX_INDEX, MOD_MENU_VIEW_SPACE_X, MOD_MENU_VIEW_SPACE_Y, MOD_MENU_VIEW_SPACE_Z);
    setMessageBoxLineAndRowSizes(MOD_MENU_MESSAGE_BOX_INDEX, MOD_MENU_TEXT_LINE_WIDTH, MOD_MENU_TEXT_ROWS);
    setMessageBoxSpacing(MOD_MENU_MESSAGE_BOX_INDEX, 0, 2);
    setMessageBoxFont(MOD_MENU_MESSAGE_BOX_INDEX, 14, 14, (u8*)FONT_TEXTURE_BUFFER, (u16*)FONT_PALETTE_2_BUFFER);
    setMessageBoxInterpolationWithFlags(MOD_MENU_MESSAGE_BOX_INDEX, 1, 3);
    setMessageBoxButtonMask(MOD_MENU_MESSAGE_BOX_INDEX, 0);
    setMessageBoxScrollSpeed(MOD_MENU_MESSAGE_BOX_INDEX, 1);
    loadModMenuWindowSprite();

}

static void moveModMenuCategory(s8 direction) {

    if (direction > 0) {
        modMenuState.categoryIndex++;

        if (modMenuState.categoryIndex >= modMenuCategoryCount) {
            modMenuState.categoryIndex = 0;
        }
    } else {
        if (modMenuState.categoryIndex == 0) {
            modMenuState.categoryIndex = modMenuCategoryCount - 1;
        } else {
            modMenuState.categoryIndex--;
        }
    }

    modMenuState.itemIndex = 0;
}

static void moveModMenuItem(s8 direction) {

    ModMenuCategory *category;

    category = &modMenuCategories[modMenuState.categoryIndex];

    if (direction > 0) {
        modMenuState.itemIndex++;

        if (modMenuState.itemIndex >= category->bindingCount) {
            modMenuState.itemIndex = 0;
        }
    } else {
        if (modMenuState.itemIndex == 0) {
            modMenuState.itemIndex = category->bindingCount - 1;
        } else {
            modMenuState.itemIndex--;
        }
    }

}

bool checkModMenuOpen(void) {
    return modMenuState.open;
}

bool checkModMenuOpenShortcut(void) {

    if (checkButtonHeld(CONTROLLER_1, BUTTON_L) && checkButtonPressed(CONTROLLER_1, BUTTON_R)) {
        return TRUE;
    }

    if (checkButtonHeld(CONTROLLER_1, BUTTON_R) && checkButtonPressed(CONTROLLER_1, BUTTON_L)) {
        return TRUE;
    }

    return FALSE;

}

void openModMenu(void) {

    if (!modMenuState.open) {
        modMenuState.open = TRUE;
        modMenuState.categoryIndex = 0;
        modMenuState.itemIndex = 0;
        initializeModMenuMessageBox();
        refreshModMenuText();
        playSfx(0);
    }

}

void closeModMenu(void) {

    if (modMenuState.open) {
        modMenuState.open = FALSE;
        deactivateMessageBox(MOD_MENU_MESSAGE_BOX_INDEX);
        deactivateSprite(MOD_MENU_WINDOW_SPRITE);
    }

}

static void applyModMenuPendingChanges(u8 applyMode) {

    u8 i;

    for (i = 0; i < modMenuBindingCount; i++) {
        if (modMenuBindings[i].applyMode == applyMode) {
            applyPendingModMenuBindingValue(&modMenuBindings[i]);
        }
    }

}

void applyModMenuPendingChangesForNewDay(void) {
    applyModMenuPendingChanges(MOD_MENU_APPLY_ON_NEW_DAY);
}

void applyModMenuPendingChangesForMapLoad(void) {
    applyModMenuPendingChanges(MOD_MENU_APPLY_ON_MAP_LOAD);
}

void updateModMenu(void) {

    ModMenuBinding *binding;

    if (!modMenuState.open) {
        return;
    }

    if (checkButtonPressed(CONTROLLER_1, BUTTON_B) || checkButtonPressed(CONTROLLER_1, BUTTON_START)) {
        closeModMenu();
        playSfx(1);
        return;
    }

    if (checkButtonPressed(CONTROLLER_1, BUTTON_R)) {
        moveModMenuCategory(1);
        refreshModMenuText();
        playSfx(2);
        return;
    }

    if (checkButtonPressed(CONTROLLER_1, BUTTON_L)) {
        moveModMenuCategory(-1);
        refreshModMenuText();
        playSfx(2);
        return;
    }

    if (checkButtonRepeat(CONTROLLER_1, MOD_MENU_INPUT_DOWN)) {
        moveModMenuItem(1);
        refreshModMenuText();
        playSfx(2);
        return;
    }

    if (checkButtonRepeat(CONTROLLER_1, MOD_MENU_INPUT_UP)) {
        moveModMenuItem(-1);
        refreshModMenuText();
        playSfx(2);
        return;
    }

    binding = getSelectedModMenuBinding();

    if (checkButtonRepeat(CONTROLLER_1, MOD_MENU_INPUT_RIGHT)) {
        adjustModMenuBindingValue(binding, 1);
        refreshModMenuText();
        playSfx(0);
        return;
    }

    if (checkButtonRepeat(CONTROLLER_1, MOD_MENU_INPUT_LEFT)) {
        adjustModMenuBindingValue(binding, -1);
        refreshModMenuText();
        playSfx(0);
        return;
    }

    if (checkButtonPressed(CONTROLLER_1, BUTTON_A)) {
        if (binding->applyMode == MOD_MENU_APPLY_MANUAL) {
            applyPendingModMenuBindingValue(binding);
            refreshModMenuText();
            playSfx(0);
        }
        return;
    }

    if (checkButtonPressed(CONTROLLER_1, BUTTON_Z)) {
        resetModMenuBindingValue(binding);
        refreshModMenuText();
        playSfx(0);
        return;
    }

}

#else

bool checkModMenuOpen(void) {
    return FALSE;
}

bool checkModMenuOpenShortcut(void) {
    return FALSE;
}

void openModMenu(void) {
}

void closeModMenu(void) {
}

void updateModMenu(void) {
}

void applyModMenuPendingChangesForNewDay(void) {
}

void applyModMenuPendingChangesForMapLoad(void) {
}

#endif
