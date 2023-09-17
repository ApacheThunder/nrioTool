#include <nds.h>
#include <nds/bios.h>
#include "twl_slot.h"

// #define SD_IRQ_STATUS (*(vu32*)0x400481C)

volatile bool exitflag = false;

bool SlotResetCheck = false;

// void my_installSystemFIFO(void);
// void my_sdmmc_get_cid(int devicenumber, u32 *cid);

void VcountHandler() { inputGetAndSend(); }

void VblankHandler(void) { }

//---------------------------------------------------------------------------------
void powerButtonCB() {
//---------------------------------------------------------------------------------
	exitflag = true;
}

//---------------------------------------------------------------------------------
int main() {
//---------------------------------------------------------------------------------
	/* *(vu32*)0x400481C = 0;	// Clear SD IRQ stat register
	*(vu32*)0x4004820 = 0;	// Clear SD IRQ mask register*/

	readUserSettings();
	
	/*bool UnlockedSCFG = false;
	
	if (REG_SCFG_EXT != 0x00000000) {
		REG_SCFG_EXT=0x83FF0300;
		REG_SCFG_CLK=0x187;
		UnlockedSCFG = true;
	}*/
	
	irqInit();
	// Start the RTC tracking IRQ
	initClockIRQ();
	// touchInit();
	fifoInit();

	SetYtrigger(80);
	
	/*if (UnlockedSCFG) { 
		my_installSystemFIFO(); 
	} else {
		installSystemFIFO();
	}*/
	installSystemFIFO();
	// installSoundFIFO();

	irqSet(IRQ_VCOUNT, VcountHandler);
	irqSet(IRQ_VBLANK, VblankHandler);

	irqEnable( IRQ_VBLANK | IRQ_VCOUNT );
	
	// REG_IPC_SYNC|=IPC_SYNC_IRQ_ENABLE;
	
	setPowerButtonCB(powerButtonCB);
	
	// fifoSendValue32(FIFO_USER_03, 1);
	
	// Keep the ARM7 mostly idle
	while (!exitflag) {
		if (0 == (REG_KEYINPUT & (KEY_SELECT | KEY_START | KEY_L | KEY_R))) { exitflag = true; }
		/*if (UnlockedSCFG) {
			if (*(u32*)(0x2FFFD0C) == 0x454D4D43) {
				my_sdmmc_get_cid(true, (u32*)0x2FFD7BC);	// Get eMMC CID
				*(u32*)(0x2FFFD0C) = 0;
			}
			resyncClock();
			// Send SD status
			if(isDSiMode() || *(u16*)(0x4004700) != 0) fifoSendValue32(FIFO_USER_04, SD_IRQ_STATUS);
		}*/
		// swiWaitForVBlank();
		if(!SlotResetCheck & fifoCheckValue32(FIFO_USER_01)) {
			TWL_ResetSlot1();
			fifoSendValue32(FIFO_USER_01, 0);
			fifoSendValue32(FIFO_USER_02, 1);
			SlotResetCheck = true;
		}
		swiIntrWait(1,IRQ_FIFO_NOT_EMPTY | IRQ_VBLANK);
	}
	return 0;
}

