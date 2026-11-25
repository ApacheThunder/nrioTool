#include <nds.h>
#include <stdio.h>
#include <stdlib.h>

#include "ui.h"
#include "font.h"

#include "mainMenu.h"
#include "mainMenuDebug.h"
#include "advancedMenu.h"
#include "advancedMenuDebug.h"
#include "dldiMenu.h"
#include "dldiMenuDebug.h"
#include "utilityMenu.h"
#include "utilityMenuDebug.h"
#include "botConsole.h"

static PrintConsole tpConsole;
static PrintConsole btConsole;
static unsigned queuedPalette = 15;

static int bg;
static int bgSub;

bool TopSelected = false;

extern bool debugMode;

void SelectTopConsole() { consoleSelect(&tpConsole); }
void SelectBotConsole() { consoleSelect(&btConsole); }
int BottomConsoleRow() { return btConsole.windowY; }
void BottomConsoleQueuedColor(bool enabled) {
	btConsole.fontCurPal = (enabled ? queuedPalette : 15) << 12;
}

void consoleClearTop(bool KeepTopSelected) {
	SelectTopConsole();
	consoleClear();
	if (!KeepTopSelected)SelectBotConsole();
}

void PrintToTop(const char* Message, int data, bool clearScreen) {
	if (!TopSelected) { SelectTopConsole(); TopSelected = true; }
	if (clearScreen)consoleClear();
	if (data != -1) { iprintf(Message, data); } else { printf(Message); }
	SelectBotConsole();
	TopSelected = false;
}

void LoadTopScreenSplash() {	
	dmaCopy(mainMenuBitmap, bgGetGfxPtr(bg), 256*256);
	dmaCopy(mainMenuPal, BG_PALETTE, 256*2);
	BG_PALETTE[255] = RGB15(31,31,31);
}

void LoadTopScreenDebugSplash() {	
	dmaCopy(mainMenuDebugBitmap, bgGetGfxPtr(bg), 256*256);
	dmaCopy(mainMenuDebugPal, BG_PALETTE, 256*2);
	BG_PALETTE[255] = RGB15(31,31,31);
}

void LoadTopScreenDLDISplash() {
	if (debugMode) {
		dmaCopy(dldiMenuDebugBitmap, bgGetGfxPtr(bg), 256*256);
		dmaCopy(dldiMenuDebugPal, BG_PALETTE, 256*2);
	} else {
		dmaCopy(dldiMenuBitmap, bgGetGfxPtr(bg), 256*256);
		dmaCopy(dldiMenuPal, BG_PALETTE, 256*2);
	}
	BG_PALETTE[255] = RGB15(31,31,31);
}

void LoadTopScreenUtilitySplash(bool showDebug) {
	dmaCopy(showDebug ? utilityMenuDebugBitmap : utilityMenuBitmap, bgGetGfxPtr(bg), 256*256);
	dmaCopy(showDebug ? utilityMenuDebugPal : utilityMenuPal, BG_PALETTE, 256*2);
	BG_PALETTE[255] = RGB15(31,31,31);
}

void LoadTopScreenAdvancedSplash(bool showDebug) {
	dmaCopy(showDebug ? advancedMenuDebugBitmap : advancedMenuBitmap, bgGetGfxPtr(bg), 256*256);
	dmaCopy(showDebug ? advancedMenuDebugPal : advancedMenuPal, BG_PALETTE, 256*2);
	BG_PALETTE[255] = RGB15(31,31,31);
}

void BootSplashInit() {
	videoSetMode(MODE_4_2D);
	videoSetModeSub(MODE_4_2D);
	vramSetBankA (VRAM_A_MAIN_BG);
	vramSetBankC (VRAM_C_SUB_BG);
	
	bg = bgInit(3, BgType_Bmp8, BgSize_B8_256x256, 1, 0);
	bgSub = bgInitSub(3, BgType_Bmp8, BgSize_B8_256x256, 1, 0);
	
	consoleInit(&btConsole, 0, BgType_Text4bpp, BgSize_T_256x256, 4, 6, false, false);
	consoleInit(&tpConsole, 0, BgType_Text4bpp, BgSize_T_256x256, 4, 6, true, false);
		
	ConsoleFont font;
	font.gfx = (u16*)fontTiles;
	font.pal = (u16*)fontPal;
	font.numChars = 95;
	font.numColors =  fontPalLen / 2;
	font.bpp = 4;
	font.asciiOffset = 32;
	font.convertSingleColor = true;
	consoleSetFont(&btConsole, &font);
	consoleSetFont(&tpConsole, &font);
	
	consoleSetWindow(&btConsole, 0, MENU_CONSOLE_TOP, 32, 24 - MENU_CONSOLE_TOP);
	consoleSetWindow(&tpConsole, 1, 5, 30, 19);
	
	// Load graphics after font or else you get palette conflicts. :P
	dmaCopy(mainMenuBitmap, bgGetGfxPtr(bg), 256*256);
	dmaCopy(mainMenuPal, BG_PALETTE, 256*2);
	dmaCopy(botConsoleBitmap, bgGetGfxPtr(bgSub), 256*256);
	dmaCopy(botConsolePal, BG_PALETTE_SUB, 256*2);
	
	BG_PALETTE[255] = RGB15(31,31,31);
	BG_PALETTE_SUB[255] = RGB15(31,31,31);
    // The bitmap and text share BG_PALETTE_SUB. Pick a text ink slot
    // that is unused by the bitmap so the highlight doesn't recolor it.
    const u8 *backdrop = reinterpret_cast<const u8 *>(botConsoleBitmap);
    bool used[256] = {};
    for (unsigned i = 0; i < 256 * 256; ++i) used[backdrop[i]] = true;
    for (unsigned bank = 0; bank < 15; ++bank) {
        unsigned ink = bank * 16 + 15;
        if (!used[ink]) {
            BG_PALETTE_SUB[ink] = RGB15(31, 8, 8);
            queuedPalette = bank;
            break;
        }
    }

	
	consoleSelect(&btConsole);
}

