#include "App_Drive.h"

#include "App_Shared.h"
#include "FreeRTOS.h"
#include "task.h"
#include "Motor.h"
#include "App_Lamp.h"

#define DRIVE_TASK_PERIOD_MS        (10U)
#define DRIVE_CMD_STALE_MS          (300U)
#define DUTY_MIN                    (0)
#define DUTY_MAX                    (100)
#define DRIVE_TASK_STACK_WORDS      (configMINIMAL_STACK_SIZE + 256U)
#define DRIVE_TASK_PRIORITY         (6U)

static TaskHandle_t g_driveTaskHandle = NULL;

static inline int clampi(int value, int lo, int hi)
{
    if (value < lo)
    {
        return lo;
    }
    if (value > hi)
    {
        return hi;
    }
    return value;
}

static void task_motor_control(void *arg)
{
    (void)arg;

    TickType_t lastWake = xTaskGetTickCount();
    const TickType_t periodTicks = pdMS_TO_TICKS(DRIVE_TASK_PERIOD_MS);
    const TickType_t staleTicks = pdMS_TO_TICKS(DRIVE_CMD_STALE_MS);

    int currentLeft = 0;
    int currentRight = 0;
    int currentLeftDir = 1;
    int currentRightDir = 1;

    for (;;)
    {
        vTaskDelayUntil(&lastWake, periodTicks);

        TickType_t now = xTaskGetTickCount();
        DriveCommand cmd;
        bool haveFresh = AppShared_GetCommandIfFresh(&cmd, now, staleTicks);
        bool aebActive = AppShared_IsAebActive();
        bool diagActive = AppShared_IsDiagSessionActive(now);
        uint8_t overrideDir = 0U;
        uint8_t overrideSpeed = 0U;
        bool haveOverride = AppShared_GetMotorOverride(&overrideDir, &overrideSpeed);

        int targetLeft = 0;
        int targetRight = 0;
        int targetLeftDir = 1;
        int targetRightDir = 1;

        //cmd가 유효하면
        if (diagActive && haveOverride)
        {
            targetLeft = clampi(overrideSpeed, DUTY_MIN, DUTY_MAX);
            targetRight = clampi(overrideSpeed, DUTY_MIN, DUTY_MAX);
            targetLeftDir = overrideDir ? 1 : 0;
            targetRightDir = overrideDir ? 1 : 0;
        }
        else if (haveFresh)
        {
            targetLeft = clampi(cmd.left_duty, DUTY_MIN, DUTY_MAX);
            targetRight = clampi(cmd.right_duty, DUTY_MIN, DUTY_MAX);
            targetLeftDir = cmd.left_dir ? 1 : 0;
            targetRightDir = cmd.right_dir ? 1 : 0;
        }
        //AEB가 활성화되어있고 전진상태라면 정지
        if (!diagActive && aebActive && (targetLeftDir == 1) && (targetRightDir == 1))
        {
            targetLeft = 0;
            targetRight = 0;
        }

        // 깜빡이 상태 업데이트: 최종 타겟 듀티 기준
        AppLamp_UpdateBySpeeds(targetLeft, targetRight);
        // 속도 즉시 적용 (램프 기능 제거)
        currentLeft = targetLeft;
        currentRight = targetRight;
        //방향 조절
        currentLeftDir = targetLeftDir;
        currentRightDir = targetRightDir;
        //모터 구동
        if (currentLeft == 0)
        {
            Motor_stopChA();
        }
        else
        {
            Motor_movChA_PWM(currentLeft, currentLeftDir);
        }

        if (currentRight == 0)
        {
            Motor_stopChB();
        }
        else
        {
            Motor_movChB_PWM(currentRight, currentRightDir);
        }
    }
}

void AppDrive_Init(void)
{
    if (g_driveTaskHandle != NULL)
    {
        return;
    }

    AppLamp_Init();
    Motor_Init();
    Motor_stopChA();
    Motor_stopChB();

    BaseType_t ok = xTaskCreate(task_motor_control,
                                "drive",
                                DRIVE_TASK_STACK_WORDS,
                                NULL,
                                DRIVE_TASK_PRIORITY,
                                &g_driveTaskHandle);
    configASSERT(ok == pdPASS);
}
