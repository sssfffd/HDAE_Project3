#ifndef APP_SHARED_H_
#define APP_SHARED_H_

#include <stdbool.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

/* Shared command/state types and helpers across driving subsystems. */

typedef struct
{
    int left_duty;
    int right_duty;
    int left_dir;
    int right_dir;
    TickType_t timestamp;
    bool valid;
} DriveCommand;

void AppShared_Init(void);

bool AppShared_ParseBleCommand(const char *line, DriveCommand *out_cmd);

void AppShared_SetCommand(const DriveCommand *cmd);
bool AppShared_GetCommand(DriveCommand *out_cmd);
bool AppShared_GetCommandIfFresh(DriveCommand *out_cmd, TickType_t now, TickType_t stale_timeout_ticks);

void AppShared_SetAebActive(bool active);
bool AppShared_IsAebActive(void);
void AppShared_SetAebActiveFromIsr(bool active);

void AppShared_SetDiagSession(bool active, TickType_t expire_tick);
bool AppShared_IsDiagSessionActive(TickType_t now);

void AppShared_SetMotorOverride(uint8_t dir, uint8_t speed);
void AppShared_ClearMotorOverride(void);
bool AppShared_GetMotorOverride(uint8_t *dir, uint8_t *speed);

#endif /* APP_SHARED_H_ */
