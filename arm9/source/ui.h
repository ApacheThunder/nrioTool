#ifndef UI_H
#define UI_H

#ifdef __cplusplus
extern "C" {
#endif

void SelectTopConsole(void);
void SelectBotConsole(void);
void consoleClearTop(bool KeepTopSelected);

void PrintToTop(const char* Message, int data, bool clearScreen);

void DoFATerror(bool isFatel, bool isSDError);

void LoadTopScreenSplash(void);
void LoadTopScreenDLDISplash(void);
void LoadTopScreenUtilitySplash(void);
void BootSplashInit(void);

#ifdef __cplusplus
}
#endif

#endif

