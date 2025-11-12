/**********************************************************************************************************************
 * \file ConfigurationIsr.h
 * Minimal ISR priorities to satisfy Ethernet/LwIP integration
 *********************************************************************************************************************/

#ifndef CONFIGURATIONISR_H
#define CONFIGURATIONISR_H

#define ISR_PRIORITY_OS_TICK        99
#define ISR_PRIORITY_GETH_TX        100
#define ISR_PRIORITY_GETH_RX        101

#endif /* CONFIGURATIONISR_H */

