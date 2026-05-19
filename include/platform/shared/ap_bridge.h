#ifndef AP_BRIDGE_H
#define AP_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "gba/types.h"

// Connection parameters bound to the ImGui menu
extern char gApServerIp[128];
extern char gApSlotName[64];
extern char gApPassword[64];

// Status flags
extern bool8 gApIsConnected;
extern char gApStatusMessage[256];

void AP_Init(void);
void AP_Update(void);
void AP_Connect(void);
void AP_SendLocationCheck(u8 zone, u8 act, bool8 isBoss);
void AP_SendChaosEmeraldCheck(u8 emeraldId);

#ifdef __cplusplus
}
#endif

#endif // AP_BRIDGE_H
