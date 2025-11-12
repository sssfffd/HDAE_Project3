/**
 * System orchestration for FreeRTOS tasks on the RC car platform.
 *
 * Responsibilities:
 *   - Initialize shared state containers
 *   - Bring up communication (SOME/IP), safety (AEB), and drive control modules
 */

#include "App_Shared.h"
#include "App_Comm.h"
#include "App_AEB.h"
#include "App_Drive.h"

#include "App_RCar.h"

void RCar_CreateTasks(void)
{
    AppShared_Init();
    AppComm_Init();
    AppAEB_Init();
    AppDrive_Init();
}
