#include <nds.h>
#include <nds/bios.h>
#include <string.h>
#include "my_sdmmc.h"

#define SD_IRQ_STATUS (*(vu32*)0x400481C)

#define BASE_DELAY (100)

volatile bool exitflag = false;

void my_installSystemFIFO(void);
void my_sdmmc_get_cid(int devicenumber, u32 *cid);

u8 my_i2cReadRegister(u8 device, u8 reg);
u8 my_i2cWriteRegister(u8 device, u8 reg, u8 data);

//---------------------------------------------------------------------------------
void ReturntoDSiMenu() {
//---------------------------------------------------------------------------------
	if (isDSiMode()) {
		my_i2cWriteRegister(0x4A, 0x70, 0x01);		// Bootflag = Warmboot/SkipHealthSafety
		my_i2cWriteRegister(0x4A, 0x11, 0x01);		// Reset to DSi Menu
	} else {
		u8 readCommand = readPowerManagement(0x10);
		readCommand |= BIT(0);
		writePowerManagement(0x10, readCommand);
	}
}

void powerButtonCB() { exitflag = true; }

void VcountHandler() { inputGetAndSend(); }

void VblankHandler(void) { if(fifoCheckValue32(FIFO_USER_03))ReturntoDSiMenu(); }

//---------------------------------------------------------------------------------
int main() {
//---------------------------------------------------------------------------------
	bool UnlockedSCFG = false;
	if ((REG_SCFG_EXT & BIT(31))) {
		// __dsimode = false;
		// Enable SD/MMC access
		if (!(REG_SCFG_EXT & BIT(18)))REG_SCFG_EXT |= BIT(18);
		// Use NTR settings but keep SD/MMC bit enabled. (needs to be set in tandom with SCFG_EXT bit 18 else SD access will not work.
		if (!(REG_SCFG_EXT & BIT(0)))REG_SCFG_CLK |= BIT(0);
		UnlockedSCFG = true;
	}
	if (REG_SNDEXTCNT != 0) {
		my_i2cWriteRegister(0x4A, 0x12, 0x00);	// Press power-button for auto-reset
		my_i2cWriteRegister(0x4A, 0x70, 0x01);	// Bootflag = Warmboot/SkipHealthSafety
	}
	
	if (UnlockedSCFG) {
		*(vu32*)0x400481C = 0;	// Clear SD IRQ stat register
		*(vu32*)0x4004820 = 0;	// Clear SD IRQ mask register
	}
	readUserSettings();
	if (UnlockedSCFG)ledBlink(0);
	irqInit();
	// Start the RTC tracking IRQ
	initClockIRQ();
	if (UnlockedSCFG)touchInit();
	fifoInit();
	SetYtrigger(80);
	if (UnlockedSCFG) { my_installSystemFIFO(); } else { installSystemFIFO(); }
	irqSet(IRQ_VCOUNT, VcountHandler);
	irqSet(IRQ_VBLANK, VblankHandler);
	irqEnable( IRQ_VBLANK | IRQ_VCOUNT );
	setPowerButtonCB(powerButtonCB);
	// Keep the ARM7 mostly idle
	while (!exitflag) {
		if (0 == (REG_KEYINPUT & (KEY_SELECT | KEY_START | KEY_L | KEY_R))) { exitflag = true; }
		if (UnlockedSCFG) {
			if(*(u16*)(0x4004700) != 0)fifoSendValue32(FIFO_USER_04, SD_IRQ_STATUS);
			resyncClock();
			// Send SD status
			if(*(u16*)(0x4004700) != 0) fifoSendValue32(FIFO_USER_04, SD_IRQ_STATUS);
		}
		swiIntrWait(1,IRQ_FIFO_NOT_EMPTY | IRQ_VBLANK);
	}
	return 0;
}

