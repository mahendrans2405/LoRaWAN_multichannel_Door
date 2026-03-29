/* lora_app_door_config.h */

#ifndef LORA_APP_DOOR_CONFIG_H
#define LORA_APP_DOOR_CONFIG_H

#include "LmHandler.h"



#define ACTIVE_REGION                               LORAMAC_REGION_IN865

/* ── TX interval — must match APP_TX_DUTYCYCLE in lora_app.h ── */
#define APP_TX_DUTYCYCLE    60000U

/* ── LoRaWAN ports ─────────────────────────────────────────── */
#define PORT_DOOR_EVENT     2U    /* all uplinks              */
#define PORT_TIME_SYNC      4U    /* downlink time sync only  */

/* ── Max retries before giving up (buzzer never gives up) ──── */
#define TX_MAX_RETRIES      3U

/* ── Called from lora_app.c ────────────────────────────────── */
void Door_SendTxData(LmHandlerAppData_t *pAppData);
void Door_OnTxData(LmHandlerTxParams_t *params);
void Door_OnRxData(LmHandlerAppData_t *appData, LmHandlerRxParams_t *params);

#endif /* LORA_APP_DOOR_CONFIG_H */
