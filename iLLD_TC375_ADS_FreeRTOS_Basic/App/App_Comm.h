#ifndef APP_COMM_H_
#define APP_COMM_H_

#include <stddef.h>
#include <stdint.h>

#define APP_COMM_SOMEIP_PAYLOAD_MAX          (64U)

void AppComm_Init(void);
void AppComm_HandleDriveCommandPayload(const uint8_t *payload, uint16_t length);

#define APP_COMM_SOMEIP_METHOD_DRIVE_COMMAND   (0x0201U)

#endif /* APP_COMM_H_ */
