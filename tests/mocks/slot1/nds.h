extern volatile unsigned test_scfg_mc;
#define REG_SCFG_MC test_scfg_mc
static inline int enterCriticalSection(void) { return 1; }
static inline void leaveCriticalSection(int state) { (void)state; }
