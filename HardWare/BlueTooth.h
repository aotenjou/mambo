#ifndef __BLUE_TOOTH_H
#define __BLUE_TOOTH_H

#include <stdint.h>

#define BLUETOOTH_TARGET_NAME "guaguale_manbo"

extern volatile uint16_t Action_Mode;
extern volatile uint16_t Face_Mode;
extern volatile uint16_t SpeedDelay;
extern volatile uint16_t SwingDelay;
extern volatile uint8_t WeiBa;
extern volatile uint16_t AllLed;
extern volatile uint16_t BreatheLed;
extern volatile uint16_t Sustainedmove;
extern volatile uint8_t BlueTooth_ATReady;
extern volatile uint8_t BlueTooth_NameReady;
extern char BlueTooth_Name[32];

void BlueTooth_Init(void);
void BlueTooth_Poll(void);

#endif
