#include "lwip/opt.h"

#if NO_SYS == 0

#include "lwip/sys.h"
#include "lwip/err.h"
#include "lwip/stats.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#ifndef LWIP_ASSERT
#define LWIP_ASSERT(message, assertion) do { if (!(assertion)) { configASSERT((assertion)); } } while (0)
#endif

/*-----------------------------------------------------------
 * Mutex API
 *----------------------------------------------------------*/
err_t sys_mutex_new(sys_mutex_t *mutex)
{
    LWIP_ASSERT("mutex != NULL", mutex != NULL);

    *mutex = xSemaphoreCreateMutex();
    if (*mutex == NULL)
    {
        SYS_STATS_INC(mutex.err);
        return ERR_MEM;
    }

    SYS_STATS_INC_USED(mutex);
    return ERR_OK;
}

void sys_mutex_free(sys_mutex_t *mutex)
{
    if ((mutex == NULL) || (*mutex == NULL))
    {
        return;
    }

    vSemaphoreDelete(*mutex);
    *mutex = NULL;
    SYS_STATS_DEC(mutex.used);
}

void sys_mutex_lock(sys_mutex_t *mutex)
{
    LWIP_ASSERT("mutex != NULL", (mutex != NULL) && (*mutex != NULL));
    (void)xSemaphoreTake(*mutex, portMAX_DELAY);
}

void sys_mutex_unlock(sys_mutex_t *mutex)
{
    LWIP_ASSERT("mutex != NULL", (mutex != NULL) && (*mutex != NULL));
    xSemaphoreGive(*mutex);
}

int sys_mutex_valid(sys_mutex_t *mutex)
{
    return ((mutex != NULL) && (*mutex != NULL));
}

void sys_mutex_set_invalid(sys_mutex_t *mutex)
{
    if (mutex != NULL)
    {
        *mutex = NULL;
    }
}

/*-----------------------------------------------------------
 * Semaphore API
 *----------------------------------------------------------*/
err_t sys_sem_new(sys_sem_t *sem, u8_t count)
{
    LWIP_ASSERT("sem != NULL", sem != NULL);

    *sem = xSemaphoreCreateBinary();
    if (*sem == NULL)
    {
        SYS_STATS_INC(sem.err);
        return ERR_MEM;
    }

    if (count != 0)
    {
        xSemaphoreGive(*sem);
    }

    SYS_STATS_INC_USED(sem);
    return ERR_OK;
}

void sys_sem_free(sys_sem_t *sem)
{
    if ((sem == NULL) || (*sem == NULL))
    {
        return;
    }

    vSemaphoreDelete(*sem);
    *sem = NULL;
    SYS_STATS_DEC(sem.used);
}

void sys_sem_signal(sys_sem_t *sem)
{
    LWIP_ASSERT("sem != NULL", (sem != NULL) && (*sem != NULL));
    xSemaphoreGive(*sem);
}

u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout)
{
    LWIP_ASSERT("sem != NULL", (sem != NULL) && (*sem != NULL));

    TickType_t ticks;
    TickType_t startTick = xTaskGetTickCount();

    if (timeout == 0U)
    {
        ticks = portMAX_DELAY;
    }
    else
    {
        ticks = pdMS_TO_TICKS(timeout);
        if (ticks == 0U)
        {
            ticks = 1U;
        }
    }

    if (xSemaphoreTake(*sem, ticks) == pdTRUE)
    {
        TickType_t endTick = xTaskGetTickCount();
        return (u32_t)((endTick - startTick) * portTICK_PERIOD_MS);
    }

    return SYS_ARCH_TIMEOUT;
}

int sys_sem_valid(sys_sem_t *sem)
{
    return ((sem != NULL) && (*sem != NULL));
}

void sys_sem_set_invalid(sys_sem_t *sem)
{
    if (sem != NULL)
    {
        *sem = NULL;
    }
}

/*-----------------------------------------------------------
 * Mailbox (message queue) API
 *----------------------------------------------------------*/
err_t sys_mbox_new(sys_mbox_t *mbox, int size)
{
    LWIP_ASSERT("mbox != NULL", mbox != NULL);
    if (size <= 0)
    {
        size = 1;
    }

    *mbox = xQueueCreate((UBaseType_t)size, sizeof(void *));
    if (*mbox == NULL)
    {
        SYS_STATS_INC(mbox.err);
        return ERR_MEM;
    }

    SYS_STATS_INC_USED(mbox);
    return ERR_OK;
}

void sys_mbox_free(sys_mbox_t *mbox)
{
    if ((mbox == NULL) || (*mbox == NULL))
    {
        return;
    }

    LWIP_ASSERT("mbox empty", uxQueueMessagesWaiting(*mbox) == 0);
    vQueueDelete(*mbox);
    *mbox = NULL;
    SYS_STATS_DEC(mbox.used);
}

void sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
    LWIP_ASSERT("mbox != NULL", (mbox != NULL) && (*mbox != NULL));
    while (xQueueSend(*mbox, &msg, portMAX_DELAY) != pdPASS)
    {
    }
}

err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
    LWIP_ASSERT("mbox != NULL", (mbox != NULL) && (*mbox != NULL));
    if (xQueueSend(*mbox, &msg, 0U) == pdPASS)
    {
        return ERR_OK;
    }

    SYS_STATS_INC(mbox.err);
    return ERR_MEM;
}

err_t sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg)
{
    BaseType_t higherPriorityTaskWoken = pdFALSE;

    LWIP_ASSERT("mbox != NULL", (mbox != NULL) && (*mbox != NULL));
    if (xQueueSendFromISR(*mbox, &msg, &higherPriorityTaskWoken) == pdPASS)
    {
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
        return ERR_OK;
    }

    SYS_STATS_INC(mbox.err);
    return ERR_MEM;
}

u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout)
{
    LWIP_ASSERT("mbox != NULL", (mbox != NULL) && (*mbox != NULL));

    TickType_t ticks;
    TickType_t startTick = xTaskGetTickCount();

    if (timeout == 0U)
    {
        ticks = portMAX_DELAY;
    }
    else
    {
        ticks = pdMS_TO_TICKS(timeout);
        if (ticks == 0U)
        {
            ticks = 1U;
        }
    }

    if (xQueueReceive(*mbox, msg, ticks) == pdPASS)
    {
        TickType_t endTick = xTaskGetTickCount();
        return (u32_t)((endTick - startTick) * portTICK_PERIOD_MS);
    }

    if (msg != NULL)
    {
        *msg = NULL;
    }
    return SYS_ARCH_TIMEOUT;
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
    LWIP_ASSERT("mbox != NULL", (mbox != NULL) && (*mbox != NULL));

    if (xQueueReceive(*mbox, msg, 0U) == pdPASS)
    {
        return ERR_OK;
    }

    return SYS_MBOX_EMPTY;
}

int sys_mbox_valid(sys_mbox_t *mbox)
{
    return ((mbox != NULL) && (*mbox != NULL));
}

void sys_mbox_set_invalid(sys_mbox_t *mbox)
{
    if (mbox != NULL)
    {
        *mbox = NULL;
    }
}

/*-----------------------------------------------------------
 * Thread API
 *----------------------------------------------------------*/
sys_thread_t sys_thread_new(const char *name,
                            lwip_thread_fn thread,
                            void *arg,
                            int stacksize,
                            int prio)
{
    TaskHandle_t handle = NULL;
    UBaseType_t stackDepth;

    if (stacksize <= 0)
    {
        stacksize = DEFAULT_THREAD_STACKSIZE;
    }

    stackDepth = (UBaseType_t)stacksize / sizeof(StackType_t);
    if (stackDepth < configMINIMAL_STACK_SIZE)
    {
        stackDepth = configMINIMAL_STACK_SIZE;
    }

    if (xTaskCreate(thread, name, stackDepth, arg, (UBaseType_t)prio, &handle) != pdPASS)
    {
        LWIP_ASSERT("sys_thread_new: xTaskCreate failed", handle != NULL);
        return NULL;
    }

    return handle;
}

/*-----------------------------------------------------------
 * Misc helpers
 *----------------------------------------------------------*/
sys_prot_t sys_arch_protect(void)
{
    taskENTER_CRITICAL();
    return 1U;
}

void sys_arch_unprotect(sys_prot_t pval)
{
    (void)pval;
    taskEXIT_CRITICAL();
}

u32_t sys_now(void)
{
    return (u32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void sys_init(void)
{
    /* Nothing to initialize for FreeRTOS port */
}

#endif /* NO_SYS == 0 */
