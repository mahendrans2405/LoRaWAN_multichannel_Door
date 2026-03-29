/* door_app.h */

#ifndef INC_DOOR_APP_H_
#define INC_DOOR_APP_H_

#include <stdint.h>
#include <stdbool.h>
#include "main.h"
#include "sys_app.h"

/* ══════════════════════════════════════════════════════════
 * LOG CONTROL
 *
 * TEST_LOG  → detailed logs for debugging (disable in production)
 * PROD_LOG  → important events only (always on)
 *
 * To disable test logs for production build:
 *   comment out → //#define ENABLE_TEST_LOG
 * ══════════════════════════════════════════════════════════ */
#define ENABLE_TEST_LOG   /* comment this out for production build */

#ifdef ENABLE_TEST_LOG
  #define TEST_LOG(...)  APP_LOG(TS_ON, VLEVEL_M, __VA_ARGS__)
#else
  #define TEST_LOG(...)
#endif

#define PROD_LOG(...)    APP_LOG(TS_ON, VLEVEL_M, __VA_ARGS__)

/* ══════════════════════════════════════════════════════════
 * SETTINGS
 * ══════════════════════════════════════════════════════════ */
#define IST_OFFSET_SECONDS      19800UL   /* UTC+5:30 for log display only */
#define DOOR_BUZZER_TRIGGER_MS  30000U    /* 30s open → buzzer alert       */
#define DOOR_EVENT_QUEUE_SIZE   50U       /* max events held in queue      */

/* ══════════════════════════════════════════════════════════
 * NODE TYPE — typedef enum
 * Sent as first byte in every payload header
 * ══════════════════════════════════════════════════════════ */
typedef enum
{
    NODE_TYPE_DOOR     = 0x01,
    NODE_TYPE_TEMP     = 0x04,
    NODE_TYPE_TEMP_HUM = 0x05,
} NodeType_t;

/* ══════════════════════════════════════════════════════════
 * FIRMWARE VERSION — typedef struct
 * ══════════════════════════════════════════════════════════ */
typedef struct
{
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
} FwVersion_t;

extern const FwVersion_t FW_VERSION;   /* defined once in door_app.c */

/* ══════════════════════════════════════════════════════════
 * PAYLOAD FORMATS
 *
 * Header (4 bytes) — same in every packet:
 *   [0] NodeType_t   [1] major   [2] minor   [3] patch
 *
 * Time Series (10 bytes):
 *   [0..3] Header
 *   [4..7] UTC epoch big-endian
 *   [8]    0x00 reserved
 *   [9]    door state  1=OPEN  0=CLOSED
 *
 * Event Batch (variable):
 *   [0..3]  Header
 *   [4..7]  base UTC epoch big-endian
 *   [8]     event count N
 *   [9]     base door state
 *   [10..]  delta seconds 1 byte each
 *
 * Buzzer (9 bytes):
 *   [0..3]  Header
 *   [4]     door state  1=OPEN  0=CLOSED
 *   [5..8]  UTC epoch big-endian
 * ══════════════════════════════════════════════════════════ */

/* ── Public functions ─────────────────────────────────────── */
void DoorApp_Init(void);
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);

/* Events */
bool DoorApp_HasEvents(void);
void DoorApp_PeekBatch(uint8_t *buf, uint8_t *size, uint8_t maxSize);
void DoorApp_CommitBatch(void);
void DoorApp_DiscardBatch(void);

/* Time Series */
bool DoorApp_HasTimeSeries(void);
void DoorApp_GetTimeSeries(uint8_t *buf, uint8_t *size);
void DoorApp_CommitTimeSeries(void);
void DoorApp_DiscardTimeSeries(void);

/* Buzzer */
bool DoorApp_HasBuzzer(void);
void DoorApp_GetBuzzerEvent(uint8_t *buf, uint8_t *size);
void DoorApp_AckBuzzer(void);

#endif /* INC_DOOR_APP_H_ */
