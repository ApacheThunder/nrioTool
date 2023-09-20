#include <nds.h>
#include <nds/bios.h>
#include <string.h>
#include "my_sdmmc.h"

#define SD_IRQ_STATUS (*(vu32*)0x400481C)

volatile bool exitflag = false;

extern bool __dsimode;

void my_installSystemFIFO(void);
void my_sdmmc_get_cid(int devicenumber, u32 *cid);

u8 my_i2cReadRegister(u8 device, u8 reg);
u8 my_i2cWriteRegister(u8 device, u8 reg, u8 data);

//---------------------------------------------------------------------------------
void ReturntoDSiMenu() {
//---------------------------------------------------------------------------------
	if (isDSiMode()) {
		i2cWriteRegister(0x4A, 0x70, 0x01);		// Bootflag = Warmboot/SkipHealthSafety
		i2cWriteRegister(0x4A, 0x11, 0x01);		// Reset to DSi Menu
	} else {
		u8 readCommand = readPowerManagement(0x10);
		readCommand |= BIT(0);
		writePowerManagement(0x10, readCommand);
	}
}

void VcountHandler() { inputGetAndSend(); }

void VblankHandler(void) {
	if(fifoCheckValue32(FIFO_USER_03))ReturntoDSiMenu();
}

void powerButtonCB() { exitflag = true; }

//---------------------------------------------------------------------------------
int main() {
//---------------------------------------------------------------------------------
	bool UnlockedSCFG = false;
	if ((REG_SCFG_EXT & BIT(31))) {
		__dsimode = false;
		// Enable SD/MMC access
		REG_SCFG_EXT |= BIT(18);
		// REG_SCFG_CLK |= BIT(0);
		// Use NTR settings but keep SD/MMC bit enabled. (needs to be set in tandom with SCFG_EXT bit 18 else SD access will not work.
		REG_SCFG_CLK = 0x101;
		// Set Ram back to 4mb
		REG_SCFG_EXT &= ~(1UL << 14);
		REG_SCFG_EXT &= ~(1UL << 15);
		UnlockedSCFG = true;
	}
	
	if (UnlockedSCFG) {
		*(vu32*)0x400481C = 0;	// Clear SD IRQ stat register
		*(vu32*)0x4004820 = 0;	// Clear SD IRQ mask register
		
		// clear sound registers
		dmaFillWords(0, (void*)0x04000400, 0x100);
	
		REG_SOUNDCNT |= SOUND_ENABLE;
		writePowerManagement(PM_CONTROL_REG, ( readPowerManagement(PM_CONTROL_REG) & ~PM_SOUND_MUTE ) | PM_SOUND_AMP );
		powerOn(POWER_SOUND);
	}
	
	readUserSettings();
	
	if (UnlockedSCFG)ledBlink(0);
	
	irqInit();
	// Start the RTC tracking IRQ
	initClockIRQ();
	if (UnlockedSCFG)touchInit();
	fifoInit();
	SetYtrigger(80);
	if (UnlockedSCFG) { 
		my_installSystemFIFO(); 
	} else {
		installSystemFIFO();
	}
	irqSet(IRQ_VCOUNT, VcountHandler);
	irqSet(IRQ_VBLANK, VblankHandler);
	irqEnable( IRQ_VBLANK | IRQ_VCOUNT );
	setPowerButtonCB(powerButtonCB);
	fifoSendValue32(FIFO_RSVD_01, 1);
	// Keep the ARM7 mostly idle
	while (!exitflag) {
		if (0 == (REG_KEYINPUT & (KEY_SELECT | KEY_START | KEY_L | KEY_R))) { exitflag = true; }
		if (UnlockedSCFG) {
			if(*(u16*)(0x4004700) != 0) fifoSendValue32(FIFO_USER_04, SD_IRQ_STATUS);
			// Dump EEPROM save
			if (*(u32*)(0x2FFFD0C) == 0x454D4D43) {
				my_sdmmc_get_cid(true, (u32*)0x2FFD7BC);	// Get eMMC CID
				*(u32*)(0x2FFFD0C) = 0;
			}
			resyncClock();
			// Send SD status
			if(*(u16*)(0x4004700) != 0) fifoSendValue32(FIFO_USER_04, SD_IRQ_STATUS);
		}
		swiIntrWait(1,IRQ_FIFO_NOT_EMPTY | IRQ_VBLANK);
	}
	return 0;
}

