#include "App_DoIP.h"

#include "App_Shared.h"
#include "App_AEB.h"
#include "my_stdio.h"
#include "FreeRTOS.h"
#include "task.h"

#include "lwip/opt.h"
#include "lwip/tcp.h"
#include "lwip/tcpip.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"

#include "evadc.h"

#include <stdbool.h>
#include <string.h>

#define DOIP_TCP_PORT                    (13400U)
#define DOIP_SRC_ADDR                    (0x0201U)
#define DOIP_TARGET_ADDR                 (0x0200U)

#define DOIP_PROTOCOL_VERSION            (0x03U)
#define DOIP_PROTOCOL_VERSION_INV        (0xFCU)

#define DOIP_PAYLOAD_TYPE_ROUT_ACT_REQ   (0x0005U)
#define DOIP_PAYLOAD_TYPE_ROUT_ACT_RESP  (0x0006U)
#define DOIP_PAYLOAD_TYPE_DIAG_MSG       (0x8001U)

#define DOIP_ROUTING_ACK_SUCCESS             (0x10U)
#define DOIP_UDS_SID_READ_DTC_INFO           (0x19U)
#define DOIP_UDS_SID_READ_DATA_BY_ID         (0x22U)
#define DOIP_UDS_SID_IO_CONTROL_BY_ID        (0x2FU)
#define DOIP_UDS_SID_WRITE_DATA_BY_ID        (0x2EU)
#define DOIP_UDS_SID_DIAGNOSTIC_SESSION      (0x10U)
#define DOIP_UDS_SID_POSITIVE_OFFSET         (0x40U)

#define DOIP_DID_LIGHT                       (0x1002U)
#define DOIP_DID_MOTOR_CONTROL               (0x4000U)
#define DOIP_DID_AEB_STOP_THRESHOLD           (0x2001U)

#define UDS_NRC_SERVICE_NOT_SUPPORTED        (0x11U)
#define UDS_NRC_SUBFUNCTION_NOT_SUPPORTED    (0x12U)
#define UDS_NRC_INCORRECT_MESSAGE_LENGTH     (0x13U)
#define UDS_NRC_REQUEST_OUT_OF_RANGE         (0x31U)
#define UDS_NRC_SUBFUNCTION_NOT_ACTIVE       (0x7EU)
#define UDS_NRC_CONDITIONS_NOT_CORRECT      (0x22U)

typedef enum
{
    DOIP_STATE_NONE = 0,
    DOIP_STATE_ACCEPTED,
    DOIP_STATE_RECEIVED,
    DOIP_STATE_CLOSING
} DoipStateId;

typedef struct
{
    DoipStateId      state;
    uint8_t          retries;
    struct tcp_pcb  *pcb;
    struct pbuf     *p;  /* pending response (tx) */
    struct pbuf     *rx; /* accumulating request (rx) */
} DoipConnState;

static struct tcp_pcb *s_doipPcb = NULL;
static bool s_initScheduled = false;
static void doip_init_on_tcpip(void *arg);
static err_t doip_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t doip_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static void  doip_error(void *arg, err_t err);
static err_t doip_poll(void *arg, struct tcp_pcb *tpcb);
static err_t doip_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);
static void  doip_send(struct tcp_pcb *tpcb, DoipConnState *state);
static void  doip_close(struct tcp_pcb *tpcb, DoipConnState *state);

static struct pbuf *doip_build_response(const struct pbuf *request, uint16_t *out_payloadType);
static struct pbuf *doip_make_diag_response(uint16_t client_sa, const uint8_t *uds_payload, uint8_t uds_len);
static struct pbuf *doip_handle_sid_dsc(uint16_t client_sa, const uint8_t *diag, uint32_t diag_len);
static struct pbuf *doip_handle_sid_wdbi(uint16_t client_sa, const uint8_t *diag, uint32_t diag_len);
static struct pbuf *doip_handle_sid_rdbi(uint16_t client_sa, const uint8_t *diag, uint32_t diag_len);
static struct pbuf *doip_handle_sid_rdi(uint16_t client_sa, const uint8_t *diag, uint32_t diag_len);
static struct pbuf *doip_handle_sid_iocbi(uint16_t client_sa, const uint8_t *diag, uint32_t diag_len);
static struct pbuf *doip_make_pos_rdbi(uint16_t client_sa, uint16_t did, const uint8_t *value, uint8_t value_len);
static struct pbuf *doip_make_pos_iocbi(uint16_t client_sa, uint16_t did);
static struct pbuf *doip_make_nrc(uint16_t client_sa, uint8_t sid, uint8_t nrc);
static bool doip_fill_rdbi_payload(uint16_t did, uint8_t *out_buf, uint8_t *out_len);

void AppDoIP_Init(void)
{
    if (s_initScheduled)
    {
        return;
    }

    err_t err = tcpip_callback_with_block(doip_init_on_tcpip, NULL, 1U);
    if (err != ERR_OK)
    {
        my_printf("DoIP: tcpip_callback failed (%d)\n", (int)err);
        return;
    }
    s_initScheduled = true;
}

static void doip_init_on_tcpip(void *arg)
{
    LWIP_UNUSED_ARG(arg);

    if (s_doipPcb != NULL)
    {
        return;
    }

    s_doipPcb = tcp_new();
    if (s_doipPcb == NULL)
    {
        my_printf("DoIP: tcp_new failed\n");
        return;
    }

    err_t err = tcp_bind(s_doipPcb, IP_ADDR_ANY, DOIP_TCP_PORT);
    if (err != ERR_OK)
    {
        my_printf("DoIP: tcp_bind err=%d\n", (int)err);
        tcp_close(s_doipPcb);
        s_doipPcb = NULL;
        return;
    }

    s_doipPcb = tcp_listen(s_doipPcb);
    tcp_accept(s_doipPcb, doip_accept);
    my_printf("DoIP: listening on port %u\n", (unsigned)DOIP_TCP_PORT);
}

static err_t doip_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    LWIP_UNUSED_ARG(arg);
    LWIP_UNUSED_ARG(err);

    tcp_setprio(newpcb, TCP_PRIO_MIN);

    DoipConnState *state = (DoipConnState *)mem_malloc(sizeof(DoipConnState));
    if (state == NULL)
    {
        return ERR_MEM;
    }

    memset(state, 0, sizeof(*state));
    state->state = DOIP_STATE_ACCEPTED;
    state->pcb = newpcb;
    state->retries = 0U;
    state->p = NULL;
    state->rx = NULL;

    tcp_arg(newpcb, state);
    tcp_recv(newpcb, doip_recv);
    tcp_err(newpcb, doip_error);
    tcp_poll(newpcb, doip_poll, 4); /* poll every ~500 ms */

    state->state = DOIP_STATE_RECEIVED;
    return ERR_OK;
}

static err_t doip_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (arg == NULL)
    {
        tcp_recved(tpcb, (p != NULL) ? p->tot_len : 0);
        if (p != NULL)
        {
            pbuf_free(p);
        }
        return ERR_VAL;
    }

    DoipConnState *state = (DoipConnState *)arg;

    if ((p == NULL) || (err != ERR_OK))
    {
        if (p != NULL)
        {
            pbuf_free(p);
        }
        state->state = DOIP_STATE_CLOSING;
        if (state->p == NULL)
        {
            doip_close(tpcb, state);
        }
        else
        {
            tcp_sent(tpcb, doip_sent);
            doip_send(tpcb, state);
        }
        return ERR_OK;
    }

    tcp_recved(tpcb, p->tot_len);

    if ((state->state == DOIP_STATE_ACCEPTED) || (state->state == DOIP_STATE_RECEIVED))
    {
        if (state->p != NULL)
        {
            my_printf("DoIP: busy, request dropped\n");
            pbuf_free(p);
            return ERR_OK;
        }

        /* Accumulate RX pbufs until a full DoIP frame is present */
        if (state->rx == NULL)
        {
            state->rx = p;
        }
        else
        {
            pbuf_chain(state->rx, p);
        }

        if (state->rx->tot_len < 8U)
        {
            return ERR_OK; /* wait for more */
        }

        uint8_t hdr[8];
        pbuf_copy_partial(state->rx, hdr, 8U, 0);
        uint32_t payloadLen = ((uint32_t)hdr[4] << 24) | ((uint32_t)hdr[5] << 16) | ((uint32_t)hdr[6] << 8) | (uint32_t)hdr[7];
        uint32_t need = 8U + payloadLen;
        if (state->rx->tot_len < need)
        {
            return ERR_OK; /* wait for more */
        }

        uint16_t payloadType = 0U;
        struct pbuf *response = doip_build_response(state->rx, &payloadType);
        pbuf_free(state->rx);
        state->rx = NULL;

        if (response == NULL)
        {
            my_printf("DoIP: unsupported request (type=0x%04X)\n", (unsigned int)payloadType);
            state->state = DOIP_STATE_CLOSING;
        }
        else
        {
            state->state = DOIP_STATE_RECEIVED;
            state->p = response;
            tcp_sent(tpcb, doip_sent);
            doip_send(tpcb, state);
        }

        return ERR_OK;
    }

    /* state is already closing, discard */
    pbuf_free(p);
    return ERR_OK;
}

static void doip_error(void *arg, err_t err)
{
    LWIP_UNUSED_ARG(err);
    if (arg != NULL)
    {
        DoipConnState *state = (DoipConnState *)arg;
        if (state->p != NULL)
        {
            pbuf_free(state->p);
        }
        mem_free(state);
    }
}

static err_t doip_poll(void *arg, struct tcp_pcb *tpcb)
{
    DoipConnState *state = (DoipConnState *)arg;
    if (state == NULL)
    {
        tcp_abort(tpcb);
        return ERR_ABRT;
    }

    if (state->p != NULL)
    {
        tcp_sent(tpcb, doip_sent);
        doip_send(tpcb, state);
        return ERR_OK;
    }

    if (state->state == DOIP_STATE_CLOSING)
    {
        doip_close(tpcb, state);
    }
    return ERR_OK;
}

static err_t doip_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    LWIP_UNUSED_ARG(len);

    DoipConnState *state = (DoipConnState *)arg;
    if (state == NULL)
    {
        return ERR_VAL;
    }

    state->retries = 0U;
    if (state->p != NULL)
    {
        tcp_sent(tpcb, doip_sent);
        doip_send(tpcb, state);
        return ERR_OK;
    }

    if (state->state == DOIP_STATE_CLOSING)
    {
        doip_close(tpcb, state);
    }
    return ERR_OK;
}

static void doip_send(struct tcp_pcb *tpcb, DoipConnState *state)
{
    while ((state->p != NULL) && (state->p->len <= tcp_sndbuf(tpcb)))
    {
        struct pbuf *out = state->p;
        err_t wr_err = tcp_write(tpcb, out->payload, out->len, TCP_WRITE_FLAG_COPY);
        if (wr_err != ERR_OK)
        {
            if (wr_err == ERR_MEM)
            {
                /* try later via poll */
                break;
            }

            my_printf("DoIP: tcp_write err=%d\n", (int)wr_err);
            break;
        }

        (void)tcp_output(tpcb);

        state->p = out->next;
        if (state->p != NULL)
        {
            pbuf_ref(state->p);
        }

        while (pbuf_free(out) == 0)
        {
            /* keep trying until freed (should not loop) */
        }
    }
}

static void doip_close(struct tcp_pcb *tpcb, DoipConnState *state)
{
    tcp_arg(tpcb, NULL);
    tcp_sent(tpcb, NULL);
    tcp_recv(tpcb, NULL);
    tcp_err(tpcb, NULL);
    tcp_poll(tpcb, NULL, 0);

    if (state != NULL)
    {
        if (state->p != NULL)
        {
            pbuf_free(state->p);
        }
        if (state->rx != NULL)
        {
            pbuf_free(state->rx);
        }
        mem_free(state);
    }

    tcp_close(tpcb);
}

static struct pbuf *doip_build_response(const struct pbuf *request, uint16_t *out_payloadType)
{
    uint8_t buffer[64];
    u16_t total = request->tot_len;

    if (total > sizeof(buffer))
    {
        return NULL;
    }

    pbuf_copy_partial(request, buffer, total, 0);
    if (total < 8U)
    {
        return NULL;
    }

    uint16_t payloadType = ((uint16_t)buffer[2] << 8) | buffer[3];
    if (out_payloadType != NULL)
    {
        *out_payloadType = payloadType;
    }

    uint32_t payloadLen = ((uint32_t)buffer[4] << 24)
                        | ((uint32_t)buffer[5] << 16)
                        | ((uint32_t)buffer[6] << 8)
                        | (uint32_t)buffer[7];

    if ((payloadLen + 8U) > total)
    {
        return NULL;
    }

    const uint8_t *payload = buffer + 8U;

    switch (payloadType)
    {
        case DOIP_PAYLOAD_TYPE_ROUT_ACT_REQ:
        {
            /* 라우팅 활성화 요청: 최소 7바이트 페이로드 필요 */
            if (payloadLen < 7U)
            {
                return NULL;
            }

            uint16_t client_sa = ((uint16_t)payload[0] << 8) | payload[1];
            /* 한줄 로그: 라우팅 활성화 요청 수신 */
            my_printf("DoIP RX: RA SA=0x%04X\n", (unsigned)client_sa);
            struct pbuf *resp = pbuf_alloc(PBUF_RAW, 8U + 11U, PBUF_RAM);
            if (resp == NULL)
            {
                return NULL;
            }

            uint8_t *out = (uint8_t *)resp->payload;
            out[0] = DOIP_PROTOCOL_VERSION;
            out[1] = DOIP_PROTOCOL_VERSION_INV;
            out[2] = (uint8_t)(DOIP_PAYLOAD_TYPE_ROUT_ACT_RESP >> 8);
            out[3] = (uint8_t)(DOIP_PAYLOAD_TYPE_ROUT_ACT_RESP & 0xFF);
            out[4] = 0x00;
            out[5] = 0x00;
            out[6] = 0x00;
            out[7] = 0x0B;
            out[8] = (uint8_t)(DOIP_SRC_ADDR >> 8);
            out[9] = (uint8_t)(DOIP_SRC_ADDR & 0xFF);
            out[10] = (uint8_t)(client_sa >> 8);
            out[11] = (uint8_t)(client_sa & 0xFF);
            out[12] = DOIP_ROUTING_ACK_SUCCESS;
            out[13] = 0x00;
            out[14] = 0x00;
            out[15] = 0x00;
            out[16] = 0x00;

            resp->len = 8U + 11U;
            resp->tot_len = resp->len;
            return resp;
        }

        case DOIP_PAYLOAD_TYPE_DIAG_MSG:
        {
            /* 진단 페이로드: [0..1] ClientSA, [2..3] TargetSA, [4..] UDS */
            if (payloadLen < 5U)
            {
                return NULL;
            }

            uint16_t client_sa = ((uint16_t)payload[0] << 8) | payload[1];
            uint16_t target_sa = ((uint16_t)payload[2] << 8) | payload[3];
            (void)target_sa;

            const uint8_t *diag = payload + 4U;
            uint32_t diag_len = payloadLen - 4U;
            if (diag_len < 1U)
            {
                return NULL;
            }

            uint8_t sid = diag[0]; /* SID */
            uint16_t did_log = 0xFFFFU;
            if (diag_len >= 3U)
            {
                did_log = ((uint16_t)diag[1] << 8) | diag[2];
            }
            if (sid == DOIP_UDS_SID_DIAGNOSTIC_SESSION && diag_len >= 2U)
            {
                my_printf("DoIP RX: SID=0x%02X SUB=0x%02X\n", (unsigned)sid, (unsigned)diag[1]);
            }
            /* 한줄 로그: 진단 요청 수신 (SID/DID) */
            if ((sid == DOIP_UDS_SID_WRITE_DATA_BY_ID) && (diag_len >= 3U))
            {
                did_log = ((uint16_t)diag[1] << 8) | diag[2];
            }
            my_printf("DoIP RX: SID=0x%02X DID=0x%04X\n", (unsigned)sid, (unsigned)did_log);

            switch (sid)
            {
                case DOIP_UDS_SID_DIAGNOSTIC_SESSION:
                    return doip_handle_sid_dsc(client_sa, diag, diag_len);
                case DOIP_UDS_SID_READ_DTC_INFO:
                    return doip_handle_sid_rdi(client_sa, diag, diag_len);
                case DOIP_UDS_SID_WRITE_DATA_BY_ID:
                    return doip_handle_sid_wdbi(client_sa, diag, diag_len);
                case DOIP_UDS_SID_READ_DATA_BY_ID:
                    return doip_handle_sid_rdbi(client_sa, diag, diag_len);
                case DOIP_UDS_SID_IO_CONTROL_BY_ID:
                    return doip_handle_sid_iocbi(client_sa, diag, diag_len);
                default:
                    return doip_make_nrc(client_sa, sid, UDS_NRC_SERVICE_NOT_SUPPORTED);
            }
        }

        default:
            /* 한줄 로그: 미지원 페이로드 타입 */
            my_printf("DoIP RX: PT=0x%04X LEN=%lu\n", (unsigned)payloadType, (unsigned long)payloadLen);
            return NULL;
    }
}

static struct pbuf *doip_make_diag_response(uint16_t client_sa, const uint8_t *uds_payload, uint8_t uds_len)
{
    if (uds_payload == NULL)
    {
        return NULL;
    }

    uint8_t payload_len = (uint8_t)(4U + uds_len);
    struct pbuf *resp = pbuf_alloc(PBUF_RAW, 8U + payload_len, PBUF_RAM);
    if (resp == NULL)
    {
        return NULL;
    }

    uint8_t *out = (uint8_t *)resp->payload;
    out[0] = DOIP_PROTOCOL_VERSION;
    out[1] = DOIP_PROTOCOL_VERSION_INV;
    out[2] = (uint8_t)(DOIP_PAYLOAD_TYPE_DIAG_MSG >> 8);
    out[3] = (uint8_t)(DOIP_PAYLOAD_TYPE_DIAG_MSG & 0xFF);
    out[4] = 0x00;
    out[5] = 0x00;
    out[6] = 0x00;
    out[7] = payload_len;
    out[8] = (uint8_t)(DOIP_SRC_ADDR >> 8);
    out[9] = (uint8_t)(DOIP_SRC_ADDR & 0xFF);
    out[10] = (uint8_t)(client_sa >> 8);
    out[11] = (uint8_t)(client_sa & 0xFF);
    memcpy(&out[12], uds_payload, uds_len);

    resp->len = 8U + payload_len;
    resp->tot_len = resp->len;
    return resp;
}

static struct pbuf *doip_handle_sid_dsc(uint16_t client_sa, const uint8_t *diag, uint32_t diag_len)
{
    if (diag_len < 2U)
    {
        return doip_make_nrc(client_sa, DOIP_UDS_SID_DIAGNOSTIC_SESSION, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
    }

    uint8_t subFunction = diag[1];

    switch (subFunction)
    {
        case 0x03U: /* 진단 세션 진입 */
        {
            AppShared_SetDiagSession(true, 0);
            AppShared_ClearMotorOverride();
            my_printf("DoIP: diagnostic session entered\n");
            uint8_t payload[2] = { (uint8_t)(DOIP_UDS_SID_DIAGNOSTIC_SESSION + DOIP_UDS_SID_POSITIVE_OFFSET), subFunction };
            return doip_make_diag_response(client_sa, payload, (uint8_t)sizeof(payload));
        }

        case 0x01U: /* 디폴트 세션 */
        {
            AppShared_SetDiagSession(false, 0);
            AppShared_ClearMotorOverride();
            my_printf("DoIP: diagnostic session exited\n");
            uint8_t payload[2] = { (uint8_t)(DOIP_UDS_SID_DIAGNOSTIC_SESSION + DOIP_UDS_SID_POSITIVE_OFFSET), subFunction };
            return doip_make_diag_response(client_sa, payload, (uint8_t)sizeof(payload));
        }

        default:
            return doip_make_nrc(client_sa, DOIP_UDS_SID_DIAGNOSTIC_SESSION, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
    }
}

static struct pbuf *doip_handle_sid_rdi(uint16_t client_sa, const uint8_t *diag, uint32_t diag_len)
{
    if (diag_len < 3U)
    {
        return doip_make_nrc(client_sa, DOIP_UDS_SID_READ_DTC_INFO, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
    }

    uint8_t subFunction = diag[1];
    if (subFunction != 0x02U)
    {
        return doip_make_nrc(client_sa, DOIP_UDS_SID_READ_DTC_INFO, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
    }

    uint8_t statusMask = diag[2];

    uint8_t uds_payload[4] = {0};
    uds_payload[0] = (uint8_t)(DOIP_UDS_SID_READ_DTC_INFO + DOIP_UDS_SID_POSITIVE_OFFSET);
    uds_payload[1] = subFunction;
    uds_payload[2] = 0x00U; /* statusAvailabilityMask: no DTCs */
    uds_payload[3] = statusMask;

    my_printf("DoIP RX: SID=0x19 SUB=0x%02X MASK=0x%02X, DTCs=0\n",
              (unsigned)subFunction,
              (unsigned)statusMask);

    return doip_make_diag_response(client_sa, uds_payload, (uint8_t)sizeof(uds_payload));
}

static struct pbuf *doip_make_pos_rdbi(uint16_t client_sa, uint16_t did, const uint8_t *value, uint8_t value_len)
{
    if ((value_len > 0U) && (value == NULL))
    {
        return NULL;
    }

    uint8_t uds_payload[3 + 8] = {0}; /* room for SID + DID + up to 8 bytes of data */
    if (value_len > (sizeof(uds_payload) - 3U))
    {
        return NULL;
    }

    uds_payload[0] = (uint8_t)(DOIP_UDS_SID_READ_DATA_BY_ID + DOIP_UDS_SID_POSITIVE_OFFSET);
    uds_payload[1] = (uint8_t)(did >> 8);
    uds_payload[2] = (uint8_t)(did & 0xFF);
    if (value_len > 0U)
    {
        memcpy(&uds_payload[3], value, value_len);
    }

    return doip_make_diag_response(client_sa, uds_payload, (uint8_t)(3U + value_len));
}

static struct pbuf *doip_make_pos_iocbi(uint16_t client_sa, uint16_t did)
{
    uint8_t uds_payload[3] =
    {
        (uint8_t)(DOIP_UDS_SID_IO_CONTROL_BY_ID + DOIP_UDS_SID_POSITIVE_OFFSET),
        (uint8_t)(did >> 8),
        (uint8_t)(did & 0xFF)
    };

    return doip_make_diag_response(client_sa, uds_payload, (uint8_t)sizeof(uds_payload));
}

static struct pbuf *doip_make_nrc(uint16_t client_sa, uint8_t sid, uint8_t nrc)
{
    uint8_t uds_payload[3] = { 0x7FU, sid, nrc };
    return doip_make_diag_response(client_sa, uds_payload, (uint8_t)sizeof(uds_payload));
}

static struct pbuf *doip_handle_sid_rdbi(uint16_t client_sa, const uint8_t *diag, uint32_t diag_len)
{
    if (diag_len < 3U)
    {
        return doip_make_nrc(client_sa, DOIP_UDS_SID_READ_DATA_BY_ID, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
    }

    uint16_t did = ((uint16_t)diag[1] << 8) | diag[2];
    uint8_t value[4] = {0};
    uint8_t value_len = 0U;

    if (!doip_fill_rdbi_payload(did, value, &value_len))
    {
        return doip_make_nrc(client_sa, DOIP_UDS_SID_READ_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
    }

    return doip_make_pos_rdbi(client_sa, did, value, value_len);
}

static struct pbuf *doip_handle_sid_wdbi(uint16_t client_sa, const uint8_t *diag, uint32_t diag_len)
{
    if (diag_len < 5U)
    {
        return doip_make_nrc(client_sa, DOIP_UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
    }

    TickType_t now = xTaskGetTickCount();
    if (!AppShared_IsDiagSessionActive(now))
    {
        return doip_make_nrc(client_sa, DOIP_UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_SUBFUNCTION_NOT_ACTIVE);
    }

    uint16_t did = ((uint16_t)diag[1] << 8) | diag[2];

    switch (did)
    {
        case DOIP_DID_AEB_STOP_THRESHOLD:
        {
            uint16_t raw = ((uint16_t)diag[3] << 8) | diag[4];
            if (raw < 1U || raw > 500U)
            {
                return doip_make_nrc(client_sa, DOIP_UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
            }
            AppAEB_SetThresholds((float)raw);
            uint8_t payload[3] =
            {
                (uint8_t)(DOIP_UDS_SID_WRITE_DATA_BY_ID + DOIP_UDS_SID_POSITIVE_OFFSET),
                (uint8_t)(did >> 8),
                (uint8_t)(did & 0xFF)
            };
            return doip_make_diag_response(client_sa, payload, (uint8_t)sizeof(payload));
        }

        default:
            return doip_make_nrc(client_sa, DOIP_UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
    }
}

static struct pbuf *doip_handle_sid_iocbi(uint16_t client_sa, const uint8_t *diag, uint32_t diag_len)
{
    if (diag_len < 4U)
    {
        return doip_make_nrc(client_sa, DOIP_UDS_SID_IO_CONTROL_BY_ID, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
    }

    TickType_t now = xTaskGetTickCount();
    if (!AppShared_IsDiagSessionActive(now))
    {
        return doip_make_nrc(client_sa, DOIP_UDS_SID_IO_CONTROL_BY_ID, UDS_NRC_SUBFUNCTION_NOT_ACTIVE);
    }

    uint16_t did = ((uint16_t)diag[1] << 8) | diag[2];
    uint8_t controlType = diag[3];

    if (did != DOIP_DID_MOTOR_CONTROL)
    {
        return doip_make_nrc(client_sa, DOIP_UDS_SID_IO_CONTROL_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
    }

    switch (controlType)
    {
        case 0x03U: /* ShortTermAdjustment */
        {
            if (diag_len < 6U)
            {
                return doip_make_nrc(client_sa, DOIP_UDS_SID_IO_CONTROL_BY_ID, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
            }

            uint8_t direction = diag[4];
            uint8_t speed = diag[5];
            if (speed > 100U)
            {
                speed = 100U;
            }

            AppShared_SetMotorOverride(direction ? 1U : 0U, speed);
            return doip_make_pos_iocbi(client_sa, did);
        }

        case 0x00U: /* ReturnControlToECU */
        {
            AppShared_ClearMotorOverride();
            return doip_make_pos_iocbi(client_sa, did);
        }

        default:
            return doip_make_nrc(client_sa, DOIP_UDS_SID_IO_CONTROL_BY_ID, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
    }
}

static bool doip_fill_rdbi_payload(uint16_t did, uint8_t *out_buf, uint8_t *out_len)
{
    if ((out_buf == NULL) || (out_len == NULL))
    {
        return false;
    }

    *out_len = 0U;
    uint16_t value = 0U;

    switch (did)
    {
        case DOIP_DID_AEB_STOP_THRESHOLD:
        {
            float stop = AppAEB_GetStopThreshold();
            if (stop < 0.0f)
            {
                stop = 0.0f;
            }
            if (stop > 65535.0f)
            {
                stop = 65535.0f;
            }
            value = (uint16_t)stop;
            break;
        }
        /* EVADC 관련 DID는 DoIP 경로에서는 동작하지 않도록 비활성화 */
        /* case DOIP_DID_LIGHT:
        case 0x0001U:  legacy
            value = (uint16_t)Evadc_readVR();
            break;

        case 0x0002U:
            value = (uint16_t)Evadc_readPR();
            break; */

        default:
            return false;
    }

    out_buf[0] = (uint8_t)(value >> 8);
    out_buf[1] = (uint8_t)(value & 0xFF);
    *out_len = 2U;
    return true;
}
