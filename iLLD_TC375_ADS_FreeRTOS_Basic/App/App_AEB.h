#ifndef APP_AEB_H_
#define APP_AEB_H_

void AppAEB_Init(void);

void AppAEB_SetThresholds(float stop_cm);
float AppAEB_GetStopThreshold(void);
#endif /* APP_AEB_H_ */
