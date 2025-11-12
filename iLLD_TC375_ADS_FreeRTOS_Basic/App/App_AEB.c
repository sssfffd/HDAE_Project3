#include "App_AEB.h"

#include "App_Shared.h"
#include "FreeRTOS.h"
#include "task.h"
#include "can.h"
#include "my_stdio.h"

#include <stdint.h>

static float s_aebStopThresholdCm = 30.0f;
static float s_aebClearThresholdCm = 40.0f;
static bool g_aebActiveState = false;
static float clamp_threshold(float value)
{
    if (value < 1.0f)
    {
        return 1.0f;
    }
    if (value > 500.0f)
    {
        return 500.0f;
    }
    return value;
}

void AppAEB_SetThresholds(float stop_cm)
{
    float stop = clamp_threshold(stop_cm);
    float clear = clamp_threshold(stop_cm + 10.0f);
    taskENTER_CRITICAL();
    s_aebStopThresholdCm = stop;
    s_aebClearThresholdCm = clear;
    taskEXIT_CRITICAL();
    my_printf("AEB thresholds updated: stop=%.1f clear=%.1f\n", (double)stop, (double)clear);
}

float AppAEB_GetStopThreshold(void)
{
    float value;
    taskENTER_CRITICAL();
    value = s_aebStopThresholdCm;
    taskEXIT_CRITICAL();
    return value;
}

//AEB state 초기화, CAN 초기화, 필터 설정
void AppAEB_Init(void)
{
    g_aebActiveState = false;
    AppShared_SetAebActive(false);

    /* ToF 센서 제거: CAN 초기화는 유지하지만 수신 프레임은 무시 */
    Can_Init(BD_500K, CAN_NODE0);
    Can_SetFilterRange(0x000U, 0x7FFU);
    my_printf("AEB initialized (ToF input disabled)\n");
}

void Can_RxUserCallback(unsigned int id, const char *data, int len)
{
    (void)id;
    (void)data;
    (void)len;
    /* ToF 기능 제거: 수신된 프레임은 사용하지 않음 */
}
