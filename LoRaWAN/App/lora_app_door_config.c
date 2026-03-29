/* lora_app_door_config.c */

#include "lora_app_door_config.h"
#include "door_app.h"
#include "sys_app.h"
#include "ds3231.h"
#include <time.h>

extern I2C_HandleTypeDef hi2c2;

/* ── What was sent last window ────────────────────────────── */
#define SENT_NONE         0
#define SENT_TIME_SERIES  1
#define SENT_BUZZER       2
#define SENT_EVENT        3

static uint8_t last_sent   = SENT_NONE;
static uint8_t ts_retries  = 0;
static uint8_t evt_retries = 0;
static uint8_t buz_retries = 0;


/* ══════════════════════════════════════════════════════════
 * Door_SendTxData — called every 60s from lora_app.c
 * ══════════════════════════════════════════════════════════ */
void Door_SendTxData(LmHandlerAppData_t *pAppData)
{
    pAppData->Port = PORT_DOOR_EVENT;

    if (DoorApp_HasTimeSeries())
    {
        DoorApp_GetTimeSeries(pAppData->Buffer, &pAppData->BufferSize);
        last_sent = SENT_TIME_SERIES;
        /* PROD: show retry count */
        PROD_LOG(" TX [TS RETRY %d/%d] %d bytes\r\n",
                 ts_retries + 1, TX_MAX_RETRIES, pAppData->BufferSize);
    }
    else if (DoorApp_HasBuzzer())
    {
        DoorApp_GetBuzzerEvent(pAppData->Buffer, &pAppData->BufferSize);
        last_sent = SENT_BUZZER;
        /* PROD: buzzer TX always important */
        PROD_LOG(" TX [BUZZER] State:%d %d bytes\r\n",
                 pAppData->Buffer[4], pAppData->BufferSize);
    }
    else if (DoorApp_HasEvents())
    {
        DoorApp_PeekBatch(pAppData->Buffer, &pAppData->BufferSize, 50);
        last_sent = SENT_EVENT;
        /* PROD: show how many bytes going out */
        PROD_LOG(" TX [EVENT] %d bytes\r\n", pAppData->BufferSize);
    }
    else
    {
        DoorApp_GetTimeSeries(pAppData->Buffer, &pAppData->BufferSize);
        last_sent  = SENT_TIME_SERIES;
        ts_retries = 0;
        /* TEST: fresh TS is routine — test log only */
        TEST_LOG(" TX [TS NEW] State:%d %d bytes\r\n",
                 pAppData->Buffer[9], pAppData->BufferSize);
    }

    LmHandlerErrorStatus_t status =
        LmHandlerSend(pAppData, LORAMAC_HANDLER_CONFIRMED_MSG, false);

    if (status != LORAMAC_HANDLER_SUCCESS)
    {
        /* PROD: TX failure is always important */
        PROD_LOG(" TX failed error:%d\r\n", status);
        last_sent = SENT_NONE;
    }
}


/* ══════════════════════════════════════════════════════════
 * Door_OnTxData — ACK or no ACK after TX
 * ══════════════════════════════════════════════════════════ */
void Door_OnTxData(LmHandlerTxParams_t *params)
{
    if (params == NULL) return;

    if (params->MsgType != LORAMAC_HANDLER_CONFIRMED_MSG) goto log_frame;

    if (params->AckReceived)
    {
        ts_retries = 0; evt_retries = 0; buz_retries = 0;

        if      (last_sent == SENT_TIME_SERIES) DoorApp_CommitTimeSeries();
        else if (last_sent == SENT_BUZZER)      DoorApp_AckBuzzer();
        else if (last_sent == SENT_EVENT)       DoorApp_CommitBatch();
    }
    else
    {
        if (last_sent == SENT_TIME_SERIES)
        {
            ts_retries++;
            /* TEST: retry count detail only needed in testing */
            TEST_LOG(" No ACK [TS] %d/%d\r\n", ts_retries, TX_MAX_RETRIES);
            if (ts_retries >= TX_MAX_RETRIES)
            {
                DoorApp_DiscardTimeSeries();
                ts_retries = 0;
            }
        }
        else if (last_sent == SENT_EVENT)
        {
            evt_retries++;
            TEST_LOG(" No ACK [EVENT] %d/%d\r\n", evt_retries, TX_MAX_RETRIES);
            if (evt_retries >= TX_MAX_RETRIES)
            {
                DoorApp_DiscardBatch();
                evt_retries = 0;
            }
        }
        else if (last_sent == SENT_BUZZER)
        {
            buz_retries++;
            /* TEST: normal retry — detail only in testing */
            TEST_LOG(" No ACK [BUZZER] attempt:%d\r\n", buz_retries);
            if (buz_retries >= TX_MAX_RETRIES)
            {
                /* PROD: warn if many consecutive failures */
                PROD_LOG(" WARNING: Buzzer %d fails. Network down?\r\n",
                         buz_retries);
                buz_retries = 0;
                HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
            }
        }
    }

log_frame:
    /* TEST: frame detail only needed in testing */
    TEST_LOG(" U/L FRAME:%04d PORT:%d DR:%d PWR:%d\r\n",
             params->UplinkCounter,
             params->AppData.Port,
             params->Datarate,
             params->TxPower);
}


/* ══════════════════════════════════════════════════════════
 * Door_OnRxData — Port 4: time sync downlink only
 * ══════════════════════════════════════════════════════════ */
void Door_OnRxData(LmHandlerAppData_t *appData, LmHandlerRxParams_t *params)
{
    if (appData == NULL || params == NULL) return;
    if (appData->BufferSize == 0)         return;

    if (appData->Port == PORT_TIME_SYNC)
    {
        if (appData->BufferSize != 4)
        {
            /* PROD: bad downlink worth knowing */
            PROD_LOG(" Time Sync: wrong size %d (need 4)\r\n",
                     appData->BufferSize);
            return;
        }

        uint32_t epoch = ((uint32_t)appData->Buffer[0] << 24)
                       | ((uint32_t)appData->Buffer[1] << 16)
                       | ((uint32_t)appData->Buffer[2] <<  8)
                       |  (uint32_t)appData->Buffer[3];

        DS3231_Time_t t;
        ds3231_from_epoch(epoch, &t);
        ds3231_set_time(&hi2c2, t.seconds, t.minutes, t.hours,
                        t.weekday, t.date, t.month, t.year);

        uint32_t ist  = epoch + IST_OFFSET_SECONDS;
        struct tm *dt = gmtime((time_t *)&ist);

        /* PROD: time sync is always important */
        PROD_LOG(" Time Sync OK → %02d:%02d:%02d IST\r\n",
                 dt->tm_hour, dt->tm_min, dt->tm_sec);

        /* TEST: also print epoch for verification */
        TEST_LOG(" Time Sync epoch:%u\r\n", (unsigned int)epoch);
    }
    else
    {
        TEST_LOG(" RX port:%d ignored.\r\n", appData->Port);
    }

    if (params->RxSlot < RX_SLOT_NONE)
    {
        /* TEST: downlink frame details only in testing */
        TEST_LOG(" D/L FRAME:%04d PORT:%d DR:%d RSSI:%d SNR:%d\r\n",
                 params->DownlinkCounter,
                 appData->Port,
                 params->Datarate,
                 params->Rssi,
                 params->Snr);
    }
}
