#include <nds/system.h>
#include <nds/interrupts.h>
#include <nds/bios.h>

#define BASE_DELAY (100)

void twlEnableSlot1() {
	int oldIME = enterCriticalSection();
	while((REG_SCFG_MC & 0x0c) == 0x0c) swiDelay(1 * BASE_DELAY);
	if(!(REG_SCFG_MC & 0x0c)) {
		REG_SCFG_MC = (REG_SCFG_MC & ~0x0c) | 4;
		swiDelay(10 * BASE_DELAY);
		REG_SCFG_MC = (REG_SCFG_MC & ~0x0c) | 8;
		swiDelay(10 * BASE_DELAY);
	}
	leaveCriticalSection(oldIME);
}

void twlDisableSlot1() {
	int oldIME = enterCriticalSection();
	while((REG_SCFG_MC & 0x0c) == 0x0c) swiDelay(1 * BASE_DELAY);
	if((REG_SCFG_MC & 0x0c) == 8) {
		REG_SCFG_MC = (REG_SCFG_MC & ~0x0c) | 0x0c;
		while((REG_SCFG_MC & 0x0c) != 0) swiDelay(1 * BASE_DELAY);
	}
	leaveCriticalSection(oldIME);
}

void TWL_ResetSlot1() {
	twlDisableSlot1();
	twlEnableSlot1();
}

