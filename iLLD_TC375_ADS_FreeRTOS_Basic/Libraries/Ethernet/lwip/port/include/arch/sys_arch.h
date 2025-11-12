#ifndef ARCH_SYS_ARCH_H
#define ARCH_SYS_ARCH_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef QueueHandle_t       sys_mbox_t;
typedef SemaphoreHandle_t   sys_sem_t;
typedef SemaphoreHandle_t   sys_mutex_t;
typedef TaskHandle_t        sys_thread_t;
typedef UBaseType_t         sys_prot_t;

#define SYS_MBOX_NULL        ((QueueHandle_t)0)
#define SYS_SEM_NULL         ((SemaphoreHandle_t)0)

#ifdef __cplusplus
}
#endif

#endif /* ARCH_SYS_ARCH_H */
