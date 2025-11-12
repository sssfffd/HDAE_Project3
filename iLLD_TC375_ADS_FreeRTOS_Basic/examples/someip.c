#include "someip.h"
#include "lwip/opt.h"
#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/udp.h"
#include "GPIO.h"
#include "my_stdio.h"
#include "Configuration.h"
#include "App_Comm.h"
#include <string.h>
#include "etc.h"
#include "Ifx_Lwip.h"
#include <stdbool.h>

#if LWIP_UDP

struct udp_pcb *g_SOMEIPSD_PCB;
struct udp_pcb *g_SOMEIPSERVICE_PCB;

void SOMEIPSD_Recv_Callback(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *addr, uint16 port);
void SOMEIP_Callback(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *addr, uint16 port);

static void send_someip_response(struct udp_pcb *upcb,
                                 const ip_addr_t *addr,
                                 uint16 port,
                                 const uint8 *src_frame,
                                 uint16 frame_len,
                                 uint8 result,
                                 const char *tag)
{
    if ((upcb == NULL) || (addr == NULL) || (src_frame == NULL))
    {
        return;
    }

    struct pbuf *txbuf = pbuf_alloc(PBUF_TRANSPORT, frame_len, PBUF_RAM);
    if (txbuf == NULL)
    {
        my_printf("Failed to allocate memory for SOME/IP response (%s)\n", tag);
        return;
    }

    /* Copy request frame into response buffer, then tweak header fields */
    udp_connect(upcb, addr, port);
    pbuf_take(txbuf, src_frame, frame_len);

    if (frame_len > 15U)
    {
        uint8 *dst = (uint8 *)txbuf->payload;
        dst[14] = 0x80U; /* mark as response */
        dst[15] = result; /* return code */
    }

    ip_addr_t destination_ip;
    unsigned char a = (unsigned char)(addr->addr);
    unsigned char b = (unsigned char)(addr->addr >> 8);
    unsigned char c = (unsigned char)(addr->addr >> 16);
    unsigned char d = (unsigned char)(addr->addr >> 24);
    IP4_ADDR(&destination_ip, a, b, c, d);

    err_t err = udp_sendto(upcb, txbuf, &destination_ip, PN_SERVICE_1);
    if (err == ERR_OK)
    {
        my_printf("SOME/IP %s response sent\n", tag);
    }
    else
    {
        my_printf("SOME/IP %s response failed (%d)\n", tag, (int)err);
    }

    udp_disconnect(upcb);
    pbuf_free(txbuf);
}

void SOMEIPSD_Init(void)
{
    /* SOME/IP-SD Init */
    g_SOMEIPSD_PCB = udp_new();
    if (g_SOMEIPSD_PCB)
    {
        /* bind pcb to the 30490 port */
        /* Using IP_ADDR_ANY allow the pcb to be used by any local interface */
        err_t err = udp_bind(g_SOMEIPSD_PCB, IP_ADDR_ANY, PN_SOMEIPSD);
        delay_ms(100);
        if (err == ERR_OK) {
            /* enable broadcast so we can send to 255.255.255.255 */
            ip_set_option(g_SOMEIPSD_PCB, SOF_BROADCAST);
            /* Set a receive callback for the pcb */
udp_recv(g_SOMEIPSD_PCB, (void *)SOMEIPSD_Recv_Callback, NULL);
            my_printf("SOME/IP-SD PCB Initialized\n");
        } else {
            udp_remove(g_SOMEIPSD_PCB);
            my_printf("SOME/IP-SD PCB init failed\n");
        }
    }
}

void SOMEIP_Init(void)
{
    /* SOME/IP Service1 Init */
    g_SOMEIPSERVICE_PCB = udp_new();
    if (g_SOMEIPSERVICE_PCB)
    {
        /* bind pcb to the 5000 port */
        /* Using IP_ADDR_ANY allow the pcb to be used by any local interface */
        err_t err = udp_bind(g_SOMEIPSERVICE_PCB, IP_ADDR_ANY, PN_SERVICE_1);
        if (err == ERR_OK) {
            /* Set a receive callback for the pcb */
            udp_recv(g_SOMEIPSERVICE_PCB, (void *)SOMEIP_Callback, NULL);
            my_printf("SOME/IP Service PCB Initialized!\n");
        } else {
            udp_remove(g_SOMEIPSERVICE_PCB);
            my_printf("SOME/IP Service PCB init failed!\n");
        }
    }
}

uint32 g_InteriorLightControlCount = 0;
void SrvWelcomeLightInteriorControl(uint8 LightOnOff)
{
	/* LED on if LightOnOff=1 otherwise, LED off */
    GPIO_SetLed(1, LightOnOff);
}

uint32 g_ExteriorLightControlCount = 0;
void SrvWelcomeLightExteriorControl(uint8 LightOnOff)
{
	/* LED on if LightOnOff=1 otherwise, LED off */
    GPIO_SetLed(2, LightOnOff);
	g_ExteriorLightControlCount++;
}

void SOMEIPSD_SendOfferService(unsigned char ip_a, unsigned char ip_b, unsigned char ip_c, unsigned char ip_d)
{
	err_t err;
	uint8 MSG_OfferService[] = {
			0xFF, 0xFF, 0x81, 0x00, /* SOMEIP Header */
			0x00, 0x00, 0x00, 0x30, /* SOMEIP Header Length */
			0x00, 0x00, 0x00, 0x01, /* Request ID */
			0x01, 0x01, 0x02, 0x00, /* SOMEIP version information */
			0xC0, 0x00, 0x00, 0x00, /* SOMEIP-SD Flags*/
			0x00, 0x00, 0x00, 0x10,
			0x01, 0x00, 0x00, 0x10,
			0x01, 0x00, 0x00, 0x01,
			0x00, 0x00, 0x00, 0x0A,
			0x00, 0x00, 0x00, 0x01,
			0x00, 0x00, 0x00, 0x0C, /* Options Array */
			0x00, 0x09, 0x04, 0x00,
			0xC0, 0xA8, 0x02, 0x14, /* IP */
			0x00, 0x11, 0x77, 0x2D /* Port */
	};

	// set ip address from lwip
	uint8 *ip = Ifx_Lwip_getIpAddrPtr();
	MSG_OfferService[48] = ip[0];
	MSG_OfferService[49] = ip[1];
	MSG_OfferService[50] = ip[2];
	MSG_OfferService[51] = ip[3];

	// set supported protocol for SOME/IP
	MSG_OfferService[53] = 0x11;  // UDP only

	MSG_OfferService[54] = (uint8)(PN_SERVICE_1 >> 8) & 0xff;
	MSG_OfferService[55] = (uint8)PN_SERVICE_1 & 0xff;

	struct pbuf *txbuf = pbuf_alloc(PBUF_TRANSPORT, sizeof(MSG_OfferService), PBUF_RAM);
	if (txbuf != NULL) {
		udp_connect(g_SOMEIPSD_PCB, IP_ADDR_BROADCAST, PN_SOMEIPSD);
		pbuf_take(txbuf, MSG_OfferService, sizeof(MSG_OfferService));

		ip_addr_t destination_ip;
		IP4_ADDR(&destination_ip, ip_a, ip_b, ip_c, ip_d);
		u16_t destination_port = PN_SOMEIPSD;

        err = udp_sendto(g_SOMEIPSD_PCB, txbuf, &destination_ip, destination_port);
        if (err == ERR_OK) {
            my_printf("Send Offer Service Success !! \n");
        } else {
            my_printf("Send Offer Service Failed !! \n");
        }
		udp_disconnect(g_SOMEIPSD_PCB);

		pbuf_free(txbuf);
	} else {
		my_printf("Failed to allocate memory for UDP packet buffer.\n");
	}
}

void SOMEIPSD_SendSubEvtGrpAck(unsigned char ip_a, unsigned char ip_b, unsigned char ip_c, unsigned char ip_d)
{
	err_t err;
	uint8 MSG_SubEvtGrpAck[] = {
			0xFF, 0xFF, 0x81, 0x00, /* SOMEIP Header */
			0x00, 0x00, 0x00, 0x28, /* SOMEIP Header Length */
			0x00, 0x00, 0x00, 0x01, /* Request ID */
			0x01, 0x01, 0x02, 0x00, /* SOMEIP version information */
			0xC0, 0x00, 0x00, 0x00, /* SOMEIP-SD Flags*/
			0x00, 0x00, 0x00, 0x10,
			0x07, 0x00, 0x00, 0x00,
			0x01, 0x00, 0x00, 0x01,
			0x00, 0x00, 0x00, 0x0A,
			0x00, 0x00, 0x00, 0x01
	};

    struct pbuf *txbuf = pbuf_alloc(PBUF_TRANSPORT, sizeof(MSG_SubEvtGrpAck), PBUF_RAM);
    if (txbuf != NULL) {
        udp_connect(g_SOMEIPSD_PCB, IP_ADDR_BROADCAST, PN_SERVICE_1);
        pbuf_take(txbuf, MSG_SubEvtGrpAck, sizeof(MSG_SubEvtGrpAck));

        ip_addr_t destination_ip;
        IP4_ADDR(&destination_ip, ip_a, ip_b, ip_c, ip_d);
        u16_t destination_port = PN_SOMEIPSD;

		err = udp_sendto(g_SOMEIPSD_PCB, txbuf, &destination_ip, destination_port);
		if (err == ERR_OK) {
			my_printf("Send SOMEIP Test Message !! \n");
		} else {
			my_printf("udp_sendto fail!!\n");
		}
		udp_disconnect(g_SOMEIPSD_PCB);

		pbuf_free(txbuf);
	} else {
		my_printf(
				"Failed to allocate memory for UDP packet buffer.\n");
	}
}

void SOMEIPSD_Recv_Callback(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *addr, uint16 port)
{

	volatile uint16 ServiceID = 0;
	volatile uint16 MethodID = 0;
	volatile uint8 SD_Type = 0;

	if (p != NULL)
	{
		ServiceID = *(uint16*)(p->payload);
		MethodID  = (*(((uint8*)p->payload) + 2) << 8) +
                    *(((uint8*)p->payload) + 3);

		unsigned char a = (unsigned char)(addr->addr);
		unsigned char b = (unsigned char)(addr->addr >> 8);
		unsigned char c = (unsigned char)(addr->addr >> 16);
        unsigned char d = (unsigned char)(addr->addr >> 24);
	
		/* Received SOME/IP-SD */
		if (ServiceID == 0xFFFFU && MethodID == 0x8100U)
		{
		    SD_Type = *(((uint8*)p->payload) + 24);
			if (SD_Type == 0x00) {
				SOMEIPSD_SendOfferService(a, b, c, d);
			} else if (SD_Type == 0x06) {
				SOMEIPSD_SendSubEvtGrpAck(a, b, c, d);
			}
		}
		pbuf_free(p);
	}
}

void SOMEIP_Callback(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *addr, uint16 port)
{
    uint16 ServiceID = 0;
    uint16 MethodID = 0;
    uint8 MessageType = 0;
    /* Declarations at function scope for strict C compilers */
    int sendResponse = 0;
    const char *responseTag = "req";
    my_printf("SOME/IP Service Request Received\n");
    if (p != NULL)
    {
        const uint8 *req = (const uint8 *)p->payload;
        const uint16 req_len = p->len;
        if (req_len < 16U)
        {
            pbuf_free(p);
            return;
        }
        ServiceID = (req[0] << 8) + req[1];
        MethodID = (req[2] << 8) + req[3];
            my_printf("Requested Service ID: 0x%04X, Method ID: 0x%04X\n", ServiceID, MethodID);

		if (ServiceID != 0x0100U)
		{
			my_printf("Requested Unknown Service ID\n");
		}
		else
		{
            MessageType = req[14];
            if (MessageType == 0x00U && MethodID == APP_COMM_SOMEIP_METHOD_DRIVE_COMMAND)
            {
                uint16 payload_len = (req_len > 16U) ? (uint16)(req_len - 16U) : 0U;
                const uint8 *payload = &req[16];
                AppComm_HandleDriveCommandPayload(payload, payload_len);
                sendResponse = 1;
                responseTag = "drive";
            }
            else
            {
                my_printf("Unsupported SOME/IP Method: 0x%04X\n", MethodID);
            }
        }

        if (sendResponse)
        {
            send_someip_response(upcb, addr, port, req, req_len, 0x00U, responseTag);
        }

        pbuf_free(p);
    }
}

#endif /* LWIP_UDP */
