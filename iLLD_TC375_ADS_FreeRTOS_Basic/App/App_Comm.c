#include "App_Comm.h"

#include "App_Shared.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lwip/netif.h"
#include "Ifx_Lwip.h"
#include "geth_lwip.h"
#include "IfxGeth_Phy_Dp83825i.h"
#include "examples/someip.h"
#include "examples/DoIP.h"
#include "my_stdio.h"
#include "asclin.h"

#include <string.h>

#define SOMEIP_BROADCAST_OCTET        (255U)
#define SOMEIP_PAYLOAD_MAX            APP_COMM_SOMEIP_PAYLOAD_MAX
#define SOMEIP_TASK_STACK_WORDS       (configMINIMAL_STACK_SIZE + 1024U)
#define SOMEIP_TASK_PRIORITY          (5U)

static TaskHandle_t g_someipTaskHandle = NULL;
static bool g_offerSent = false;
static eth_addr_t g_mac = { .addr = {0x00, 0x00, 0x00, 0x11, 0x11, 0x12} };

static void task_someip_service(void *arg);
static void send_service_offer_once(void);
void AppComm_SendOffer_FromTcpip(void *arg);

void AppComm_Init(void)
{
    if (g_someipTaskHandle != NULL)
    {
        return;
    }

    /* Optional: enable UART early so logs are visible */
    Asclin1_InitUart();
    g_offerSent = false;

    BaseType_t ok = xTaskCreate(task_someip_service,
                                "someip",
                                SOMEIP_TASK_STACK_WORDS,
                                NULL,
                                SOMEIP_TASK_PRIORITY,
                                &g_someipTaskHandle);
    configASSERT(ok == pdPASS);
}

//속도값 받은거 처리
void AppComm_HandleDriveCommandPayload(const uint8_t *payload, uint16_t length)
{

    if ((payload == NULL) || (length == 0))
    {
        return;
    }

    char line[SOMEIP_PAYLOAD_MAX];
    size_t copy_len = (length < (SOMEIP_PAYLOAD_MAX - 1U)) ? length : (SOMEIP_PAYLOAD_MAX - 1U);

    memcpy(line, payload, copy_len);
    line[copy_len] = '\0';

    for (size_t i = 0U; i < copy_len; ++i)
    {
        if (line[i] == '\r' || line[i] == '\n')
        {
            line[i] = '\0';
            break;
        }
    }
    my_printf("SOME/IP payload: %s\n", line);
    DriveCommand cmd;
    if (AppShared_ParseBleCommand(line, &cmd))
    {
        AppShared_SetCommand(&cmd);
    }
    else
    {
        my_printf("SOME/IP invalid payload: %s\n", line);
    }
}

static void task_someip_service(void *arg)
{
    my_printf("SomeIP task started\n");
    (void)arg;

    static bool s_netInited = false;
    if (!s_netInited)
    {
        /* Give PHY/REFCLK time to stabilize */
        vTaskDelay(pdMS_TO_TICKS(50));
        initLwip(g_mac);
        SOMEIPSD_Init();
        SOMEIP_Init();
        DoIP_Init();
        /* MDIO probe */
        uint32 phy_id1 = 0, phy_id2 = 0, bmsr = 0;
        IfxGeth_Eth_Phy_Dp83825i_read_mdio_reg(0, 2, &phy_id1);
        IfxGeth_Eth_Phy_Dp83825i_read_mdio_reg(0, 3, &phy_id2);
        IfxGeth_Eth_Phy_Dp83825i_read_mdio_reg(0, 1, &bmsr);
        my_printf("PHY ID: %04X %04X, BMSR: %04X",
                  (unsigned int)(phy_id1 & 0xFFFF),
                  (unsigned int)(phy_id2 & 0xFFFF),
                  (unsigned int)(bmsr & 0xFFFF));
        my_printf("LwIP/SOME-IP initialized\n");
        s_netInited = true;
    }
    my_printf("SomeIP loop before\n");
    for (;;)
    {
        Ifx_Lwip_pollReceiveFlags();
        send_service_offer_once();
        vTaskDelay(pdMS_TO_TICKS(3));
    }
}

static void send_service_offer_once(void)
{
    if (g_offerSent)
    {
        return;
    }

    static TickType_t t0 = 0;
    if (t0 == 0) { t0 = xTaskGetTickCount(); }

    int link = netif_is_link_up(Ifx_Lwip_getNetIf());
    static int lastLink = -1;
    if (link != lastLink)
    {
        my_printf("Link %s", link ? "UP" : "DOWN");
        lastLink = link;
    }

    if (link || (xTaskGetTickCount() - t0) > pdMS_TO_TICKS(2000))
    {
        SOMEIPSD_SendOfferService(SOMEIP_BROADCAST_OCTET,
                                  SOMEIP_BROADCAST_OCTET,
                                  SOMEIP_BROADCAST_OCTET,
                                  SOMEIP_BROADCAST_OCTET);
        g_offerSent = true;
    }
}
