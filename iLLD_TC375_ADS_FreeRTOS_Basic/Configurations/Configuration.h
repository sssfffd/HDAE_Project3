/**********************************************************************************************************************
 * \file Configuration.h
 * Minimal configuration for Ethernet/LwIP integration in FreeRTOS project
 *********************************************************************************************************************/

#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include "Ifx_Cfg.h"
#include <_PinMap/IfxGeth_PinMap.h>
#include <_PinMap/IfxPort_PinMap.h>
#include "ConfigurationIsr.h"

/* Pins for DP83825U RMII connection */
#define ETH_CRSDIV_PIN              IfxGeth_CRSDVA_P11_11_IN
#define ETH_REFCLK_PIN              IfxGeth_REFCLKA_P11_12_IN
#define ETH_TXEN_PIN                IfxGeth_TXEN_P11_6_OUT
#define ETH_RXD0_PIN                IfxGeth_RXD0A_P11_10_IN
#define ETH_RXD1_PIN                IfxGeth_RXD1A_P11_9_IN
#define ETH_MDC_PIN                 IfxGeth_MDC_P21_2_OUT
#define ETH_MDIO_PIN                IfxGeth_MDIO_P21_3_INOUT
#define ETH_TXD0_PIN                IfxGeth_TXD0_P11_3_OUT
#define ETH_TXD1_PIN                IfxGeth_TXD1_P11_2_OUT

#define IFX_CFG_STM_TICKS_PER_MS    (100000)

#define CPU_WHICH_SERVICE_ETHERNET  0

/* SOME/IP ports */
#define PN_DoIP                         13400U
#define PN_SD                           30490U
#define PN_SERVICE_1                    30509U
#define PN_SOMEIPSD                     30490U

#define ETH_SOMEIP_PROT_VER             0x01
#define ETH_SOMEIP_IFACE_VER            0x01

#endif
