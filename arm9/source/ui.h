#ifndef UI_H
#define UI_H

#ifdef __cplusplus
extern "C" {
#endif

enum { MENU_CONSOLE_TOP = 4, MENU_CONTROLS_ROW = 22 };

void SelectTopConsole(void);
void SelectBotConsole(void);
int BottomConsoleRow(void);
void BottomConsoleQueuedColor(bool enabled);
void consoleClearTop(bool KeepTopSelected);

void PrintToTop(const char* Message, int data, bool clearScreen);

void DoFATerror(bool isFatel, bool isSDError);

void LoadTopScreenSplash(void);
void LoadTopScreenDebugSplash(void);
void LoadTopScreenDLDISplash(void);
void LoadTopScreenUtilitySplash(bool showDebug);
void LoadTopScreenAdvancedSplash(bool showDebug);
void BootSplashInit(void);

#ifdef __cplusplus
}
#endif

#endif

