#ifndef APP_DOIP_H_
#define APP_DOIP_H_

/**
 * Initialize the DoIP service.
 * Must be called after LwIP/netif setup (e.g., from App_Comm task).
 */
void AppDoIP_Init(void);

#endif /* APP_DOIP_H_ */
