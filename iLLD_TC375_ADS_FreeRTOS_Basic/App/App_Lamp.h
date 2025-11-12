/* Header-only implementation to avoid build file-list updates */
#ifndef APP_LAMP_H_
#define APP_LAMP_H_

#include <stdbool.h>
#include "Ifx_reg.h"

/*
 * Pin mapping:
 *  - P02.5 : Left turn indicator
 *  - P10.5 : Right turn indicator
 *  - P10.4 : Hazard (shared)
 */

static inline void AppLamp_Init(void)
{
    MODULE_P02.IOCR4.B.PC5 = 0x10; /* P02.5 output */
    MODULE_P10.IOCR4.B.PC5 = 0x10; /* P10.5 output */
    MODULE_P10.IOCR4.B.PC4 = 0x10; /* P10.4 output */

    MODULE_P02.OUT.B.P5 = 0;
    MODULE_P10.OUT.B.P5 = 0;
    MODULE_P10.OUT.B.P4 = 0;
}

static inline void AppLamp_SetLeft(bool on)
{
    MODULE_P02.OUT.B.P5 = on ? 1 : 0;
}

static inline void AppLamp_SetRight(bool on)
{
    MODULE_P10.OUT.B.P5 = on ? 1 : 0;
}

static inline void AppLamp_SetHazard(bool on)
{
    MODULE_P10.OUT.B.P4 = on ? 1 : 0;
}

static inline void AppLamp_UpdateBySpeeds(int left_duty, int right_duty)
{
    bool left_on = false;
    bool right_on = false;
    bool hz_on = false;

    if ((left_duty == 0) && (right_duty == 0))
    {
        left_on = true;
        right_on = true;
        hz_on = true;
    }
    else if ((left_duty == 0) && (right_duty > 0))
    {
        left_on = true;
    }
    else if ((right_duty == 0) && (left_duty > 0))
    {
        right_on = true;
    }

    AppLamp_SetLeft(left_on);
    AppLamp_SetRight(right_on);
    AppLamp_SetHazard(hz_on);
}

#endif /* APP_LAMP_H_ */
