#include <nds.h>

#include "bootsplash.h"
#include "font.h"

#include "topLogo.h"
#include "botConsole.h"

void BootSplashInit(bool isDSiMode = true) {
	videoSetMode(MODE_4_2D);
	videoSetModeSub(MODE_3_2D);
	vramSetBankA (VRAM_A_MAIN_BG);
	vramSetBankC (VRAM_C_SUB_BG);
	int bg = bgInit(3, BgType_Bmp8, BgSize_B8_256x256, 1, 0);
	int bgSub = bgInitSub(3, BgType_Bmp8, BgSize_B8_256x256, 1, 0);
	dmaCopy(topLogoBitmap, bgGetGfxPtr(bg), 256*256);
	dmaCopy(topLogoPal, BG_PALETTE, 256*2);
	
	PrintConsole *console = consoleInit(0, 0, BgType_Text4bpp, BgSize_T_256x256, 4, 6, false, false);
		
	ConsoleFont font;
	font.gfx = (u16*)fontTiles;
	font.pal = (u16*)fontPal;
	font.numChars = 95;
	font.numColors =  fontPalLen / 2;
	font.bpp = 4;
	font.asciiOffset = 32;
	font.convertSingleColor = true;
	consoleSetFont(console, &font);
	
	consoleSetWindow(console, 0, 6, 32, 24);
	
	// Load graphics after font or else you get palette conflicts. :P
	
	dmaCopy(botConsoleBitmap, bgGetGfxPtr(bgSub), 256*256);
	dmaCopy(botConsolePal, BG_PALETTE_SUB, 256*2);
	
	BG_PALETTE_SUB[255] = RGB15(31,31,31);
}

