#include <nds.h>
#include <nds/bios.h>
#include "twl_slot.h"
#include "my_sdmmc.h"

#define SD_IRQ_STATUS (*(vu32*)0x400481C)

volatile bool exitflag = false;

void my_installSystemFIFO(void);
void my_sdmmc_get_cid(int devicenumber, u32 *cid);

void VcountHandler() { inputGetAndSend(); }

void VblankHandler(void) { }

//---------------------------------------------------------------------------------
void powerButtonCB() {
//---------------------------------------------------------------------------------
	exitflag = true;
}

void DoSlotResetCheck() {
	if(fifoCheckValue32(FIFO_USER_02)) {
		int Result = fifoGetValue32(FIFO_USER_02);
		if (Result == 1) {
			TWL_ResetSlot1();
			fifoSendValue32(FIFO_USER_02, 0);
			Result = 0;
		}
	}
}


// extern bool __dsimode;

//---------------------------------------------------------------------------------
int main() {
//---------------------------------------------------------------------------------
	bool UnlockedSCFG = false;
	if (REG_SCFG_EXT != 0x00000000) {
		// Force libNDS to detect as NTR mode.
		// __dsimode = false;
		// Force NTR mode. DS-Xtreme cart does not like being in TWL mode. :P
		// if (REG_SCFG_ROM != 0x703) { REG_SCFG_ROM = 0x703; }
		// REG_SCFG_EXT=0x83FF0300;
		// REG_SCFG_EXT=0x93FFFB06;
		// REG_SCFG_EXT=0x93E00000;
		// REG_SCFG_CLK=0x101;
		REG_SCFG_CLK=0x185;
		// REG_SCFG_CLK=0x186;
		// REG_SCFG_CLK=0x181;
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
	if (UnlockedSCFG) { touchInit();}
	fifoInit();
	SetYtrigger(80);
	if (UnlockedSCFG) { 
		my_installSystemFIFO(); 
	} else {
		installSystemFIFO();
	}
	// installSoundFIFO();
	irqSet(IRQ_VCOUNT, VcountHandler);
	irqSet(IRQ_VBLANK, VblankHandler);
	irqEnable( IRQ_VBLANK | IRQ_VCOUNT );
	// REG_IPC_SYNC|=IPC_SYNC_IRQ_ENABLE;
	setPowerButtonCB(powerButtonCB);
	fifoSendValue32(FIFO_USER_01, 1);
	// Keep the ARM7 mostly idle
	while (!exitflag) {
		if (0 == (REG_KEYINPUT & (KEY_SELECT | KEY_START | KEY_L | KEY_R))) { exitflag = true; }
		if (UnlockedSCFG) {
			DoSlotResetCheck();			
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
		// swiWaitForVBlank();
		swiIntrWait(1,IRQ_FIFO_NOT_EMPTY | IRQ_VBLANK);
	}
	return 0;
}

