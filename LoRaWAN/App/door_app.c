/* door_app.c */

#include "door_app.h"
#include "ds3231.h"
#include "stm32_timer.h"
#include "utilities_def.h"
#include "lora_app.h"
#include "stm32_seq.h"
#include <time.h>

/* ── External I2C handle ──────────────────────────────────── */
extern I2C_HandleTypeDef hi2c2;

/* ── Firmware version — single definition ─────────────────── */
const FwVersion_t FW_VERSION = { .major = 1, .minor = 0, .patch = 0 };

/* ══════════════════════════════════════════════════════════
 * EVENT QUEUE
 * ══════════════════════════════════════════════════════════ */
typedef struct {
    uint8_t  state;      /* 1=OPEN  0=CLOSED */
    uint32_t timestamp;  /* UTC epoch        */
} DoorEvent_t;

static volatile DoorEvent_t queue[DOOR_EVENT_QUEUE_SIZE];
static volatile uint8_t  q_head     = 0;
static volatile uint8_t  q_tail     = 0;
static volatile uint8_t  q_count    = 0;
static volatile uint8_t  peek_count = 0;

/* ══════════════════════════════════════════════════════════
 * BUZZER STATE
 * buzzer_pending    → 1 = needs TX
 * buzzer_state      → 1=OPEN  0=CLOSED
 * buzzer_epoch      → UTC frozen at trigger or close time
 * buzzer_close_pend → door closed while OPEN TX in-flight
 * buzzer_open_acked → OPEN ACKed, door still open,
 *                     send CLOSED when door closes later
 * ══════════════════════════════════════════════════════════ */
static volatile uint8_t  buzzer_pending    = 0;
static volatile uint8_t  buzzer_state      = 0;
static volatile uint32_t buzzer_epoch      = 0;
static volatile uint8_t  buzzer_close_pend = 0;
static volatile uint8_t  buzzer_open_acked = 0;

/* ══════════════════════════════════════════════════════════
 * TIME SERIES STATE
 * ══════════════════════════════════════════════════════════ */
static volatile uint8_t  ts_pending = 0;
static volatile uint32_t ts_utc     = 0;
static volatile uint8_t  ts_state   = 0;

/* ══════════════════════════════════════════════════════════
 * TIMERS + DOOR STATE
 * ══════════════════════════════════════════════════════════ */
static UTIL_TIMER_Object_t DebounceTimer;
static UTIL_TIMER_Object_t BuzzerTimer;
static volatile uint8_t last_state = 0xFF;

static void OnDebounceTimer(void *ctx);
static void OnBuzzerTimer(void *ctx);


/* ══════════════════════════════════════════════════════════
 * DoorApp_Init
 * ══════════════════════════════════════════════════════════ */
void DoorApp_Init(void)
{
    UTIL_TIMER_Create(&DebounceTimer, 100,
                      UTIL_TIMER_ONESHOT, OnDebounceTimer, NULL);

    UTIL_TIMER_Create(&BuzzerTimer, DOOR_BUZZER_TRIGGER_MS,
                      UTIL_TIMER_ONESHOT, OnBuzzerTimer, NULL);

    last_state = (HAL_GPIO_ReadPin(door_switch_GPIO_Port,
                                   door_switch_Pin) == GPIO_PIN_SET) ? 1 : 0;

    /* PROD: always print boot state */
    PROD_LOG(" DoorApp Init. FW:%d.%d.%d Node:0x%02X State:%s\r\n",
             FW_VERSION.major, FW_VERSION.minor, FW_VERSION.patch,
             (uint8_t)NODE_TYPE_DOOR,
             last_state ? "OPEN" : "CLOSED");

    if (last_state == 1)
    {
        UTIL_TIMER_Start(&BuzzerTimer);
        PROD_LOG(" Boot: door open — buzzer countdown 30s started.\r\n");
    }
}


/* ══════════════════════════════════════════════════════════
 * GPIO INTERRUPT — do NOT add this in main.c
 * ══════════════════════════════════════════════════════════ */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin != door_switch_Pin) return;

    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
    UTIL_TIMER_Stop(&DebounceTimer);
    UTIL_TIMER_Start(&DebounceTimer);

    TEST_LOG(" EXTI: pin=0x%04X — debounce restarted.\r\n", GPIO_Pin);
}


/* ══════════════════════════════════════════════════════════
 * EVENT BATCH
 * ══════════════════════════════════════════════════════════ */
bool DoorApp_HasEvents(void)
{
    return (q_count > 0);
}

void DoorApp_PeekBatch(uint8_t *buf, uint8_t *size, uint8_t maxSize)
{
    if (q_count == 0) { *size = 0; peek_count = 0; return; }

    __disable_irq();

    uint8_t idx  = 0;
    uint8_t head = q_head;
    uint8_t left = q_count;

    /* Header */
    buf[idx++] = (uint8_t)NODE_TYPE_DOOR;
    buf[idx++] = FW_VERSION.major;
    buf[idx++] = FW_VERSION.minor;
    buf[idx++] = FW_VERSION.patch;

    /* Base event */
    DoorEvent_t base = queue[head];
    head = (head + 1) % DOOR_EVENT_QUEUE_SIZE;
    left--;

    buf[idx++] = (base.timestamp >> 24) & 0xFF;
    buf[idx++] = (base.timestamp >> 16) & 0xFF;
    buf[idx++] = (base.timestamp >>  8) & 0xFF;
    buf[idx++] =  base.timestamp        & 0xFF;

    uint8_t count_pos = idx++;
    buf[idx++] = base.state;

    uint32_t prev_ts = base.timestamp;
    uint8_t  peeked  = 1;

    while (left > 0 && idx < maxSize)
    {
        DoorEvent_t ev = queue[head];
        uint32_t delta = ev.timestamp - prev_ts;
        if (delta > 255) break;
        buf[idx++] = (uint8_t)delta;
        prev_ts    = ev.timestamp;
        peeked++;
        head = (head + 1) % DOOR_EVENT_QUEUE_SIZE;
        left--;
    }

    buf[count_pos] = peeked;
    peek_count     = peeked;

    __enable_irq();
    *size = idx;

    TEST_LOG(" PeekBatch: %d event(s) packed. %d bytes.\r\n", peeked, idx);
}

void DoorApp_CommitBatch(void)
{
    if (peek_count == 0) return;
    uint8_t n = peek_count;

    __disable_irq();
    for (uint8_t i = 0; i < n; i++)
    {
        q_head  = (q_head + 1) % DOOR_EVENT_QUEUE_SIZE;
        q_count--;
    }
    peek_count = 0;
    __enable_irq();

    /* PROD: important — data removed from queue */
    PROD_LOG(" [EVENT] ACK. %d removed. Remaining:%d\r\n", n, q_count);
}

void DoorApp_DiscardBatch(void)
{
    peek_count = 0;
    /* PROD: worth knowing max retries hit */
    PROD_LOG(" [EVENT] Max retries. peek reset. Data kept.\r\n");
}


/* ══════════════════════════════════════════════════════════
 * TIME SERIES
 * ══════════════════════════════════════════════════════════ */
bool DoorApp_HasTimeSeries(void)
{
    return (ts_pending != 0);
}

void DoorApp_GetTimeSeries(uint8_t *buf, uint8_t *size)
{
    if (!ts_pending)
    {
        ts_utc   = ds3231_read_time(&hi2c2);
        ts_state = (last_state == 0xFF)
                   ? (HAL_GPIO_ReadPin(door_switch_GPIO_Port,
                                       door_switch_Pin) == GPIO_PIN_SET ? 1 : 0)
                   : last_state;
        ts_pending = 1;

        TEST_LOG(" [TS] NEW epoch:%u state:%d\r\n",
                 (unsigned int)ts_utc, ts_state);
    }
    else
    {
        TEST_LOG(" [TS] RETRY epoch:%u state:%d\r\n",
                 (unsigned int)ts_utc, ts_state);
    }

    uint8_t idx = 0;
    buf[idx++] = (uint8_t)NODE_TYPE_DOOR;
    buf[idx++] = FW_VERSION.major;
    buf[idx++] = FW_VERSION.minor;
    buf[idx++] = FW_VERSION.patch;
    buf[idx++] = (ts_utc >> 24) & 0xFF;
    buf[idx++] = (ts_utc >> 16) & 0xFF;
    buf[idx++] = (ts_utc >>  8) & 0xFF;
    buf[idx++] =  ts_utc        & 0xFF;
    buf[idx++] = 0x00;
    buf[idx++] = ts_state;
    *size = idx;
}

void DoorApp_CommitTimeSeries(void)
{
    ts_pending = 0; ts_utc = 0; ts_state = 0;
    TEST_LOG(" [TS] ACK. Cleared.\r\n");
}

void DoorApp_DiscardTimeSeries(void)
{
    ts_pending = 0; ts_utc = 0; ts_state = 0;
    TEST_LOG(" [TS] Max retries. Discarded.\r\n");
}


/* ══════════════════════════════════════════════════════════
 * BUZZER
 * ══════════════════════════════════════════════════════════ */
bool DoorApp_HasBuzzer(void)
{
    return (buzzer_pending != 0);
}

void DoorApp_GetBuzzerEvent(uint8_t *buf, uint8_t *size)
{
    uint8_t idx = 0;
    buf[idx++] = (uint8_t)NODE_TYPE_DOOR;
    buf[idx++] = FW_VERSION.major;
    buf[idx++] = FW_VERSION.minor;
    buf[idx++] = FW_VERSION.patch;
    buf[idx++] = buzzer_state;
    buf[idx++] = (buzzer_epoch >> 24) & 0xFF;
    buf[idx++] = (buzzer_epoch >> 16) & 0xFF;
    buf[idx++] = (buzzer_epoch >>  8) & 0xFF;
    buf[idx++] =  buzzer_epoch        & 0xFF;
    *size = idx;
}

void DoorApp_AckBuzzer(void)
{
    if (buzzer_close_pend)
    {
        /* OPEN was in-flight when door closed —
         * CLOSED packet still needs to be sent next window */
        buzzer_close_pend = 0;
        PROD_LOG(" [BUZZER] OPEN ACKed. CLOSED pending — next window.\r\n");
    }
    else if (buzzer_state == 1)
    {
        /* OPEN ACKed, door still open —
         * send CLOSED buzzer when door closes later         */
        buzzer_pending    = 0;
        buzzer_open_acked = 1;
        PROD_LOG(" [BUZZER] OPEN ACKed. Waiting for door close.\r\n");
    }
    else
    {
        /* CLOSED ACKed — fully done */
        buzzer_pending    = 0;
        buzzer_open_acked = 0;
        buzzer_epoch      = 0;
        buzzer_state      = 0;
        PROD_LOG(" [BUZZER] CLOSED ACKed. All cleared.\r\n");
    }
}


/* ══════════════════════════════════════════════════════════
 * DEBOUNCE TIMER — 100ms after last GPIO edge
 * ══════════════════════════════════════════════════════════ */
static void OnDebounceTimer(void *ctx)
{
    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);

    uint8_t pin = (HAL_GPIO_ReadPin(door_switch_GPIO_Port,
                                    door_switch_Pin) == GPIO_PIN_SET) ? 1 : 0;

    if (pin == last_state)
    {
        TEST_LOG(" Debounce: no change. Ignored.\r\n");
        return;
    }

    last_state    = pin;
    uint32_t utc  = ds3231_read_time(&hi2c2);
    uint32_t ist  = utc + IST_OFFSET_SECONDS;
    struct tm *dt = gmtime((time_t *)&ist);

    /* PROD: always print door open/close with time */
    PROD_LOG(" Door %s at %02d:%02d:%02d IST\r\n",
             pin ? "OPEN" : "CLOSED",
             dt->tm_hour, dt->tm_min, dt->tm_sec);

    /* ── Door OPENED ──────────────────────────────────────── */
    if (pin == 1)
    {
        UTIL_TIMER_Start(&BuzzerTimer);
        TEST_LOG(" Buzzer countdown 30s started.\r\n");
    }
    /* ── Door CLOSED ──────────────────────────────────────── */
    else
    {
        UTIL_TIMER_Stop(&BuzzerTimer);

        if (buzzer_pending || buzzer_open_acked)
        {
            buzzer_state      = 0;
            buzzer_epoch      = utc;
            buzzer_pending    = 1;
            buzzer_close_pend = 0;
            buzzer_open_acked = 0;

            /* PROD: buzzer stop is an important event */
            PROD_LOG(" ================================\r\n");
            PROD_LOG(" [BUZZER] DOOR CLOSED — BUZZER STOPPED\r\n");
            PROD_LOG(" Closed at : %02d:%02d:%02d IST\r\n",
                     dt->tm_hour, dt->tm_min, dt->tm_sec);
            PROD_LOG(" Epoch     : %u\r\n", (unsigned int)utc);
            PROD_LOG(" Sending CLOSED buzzer now...\r\n");
            PROD_LOG(" ================================\r\n");

            UTIL_SEQ_SetTask(
                (1 << CFG_SEQ_Task_LoRaSendOnTxTimerOrButtonEvent),
                CFG_SEQ_Prio_0);
        }
    }

    /* ── Add to queue ─────────────────────────────────────── */
    DoorEvent_t ev = { .state = pin, .timestamp = utc };

    __disable_irq();
    if (q_count == DOOR_EVENT_QUEUE_SIZE)
    {
        queue[q_tail] = ev;
        q_tail = (q_tail + 1) % DOOR_EVENT_QUEUE_SIZE;
        q_head = (q_head + 1) % DOOR_EVENT_QUEUE_SIZE;
        TEST_LOG(" Queue full — oldest overwritten.\r\n");
    }
    else
    {
        queue[q_tail] = ev;
        q_tail  = (q_tail + 1) % DOOR_EVENT_QUEUE_SIZE;
        q_count++;
    }
    __enable_irq();

    /* PROD: queue count useful always */
    PROD_LOG(" Event queued. Count:%d\r\n", q_count);
}


/* ══════════════════════════════════════════════════════════
 * BUZZER TIMER — fires 30s after door opened
 * ══════════════════════════════════════════════════════════ */
static void OnBuzzerTimer(void *ctx)
{
    uint32_t utc  = ds3231_read_time(&hi2c2);
    uint32_t ist  = utc + IST_OFFSET_SECONDS;
    struct tm *dt = gmtime((time_t *)&ist);

    buzzer_epoch      = utc;
    buzzer_state      = 1;
    buzzer_pending    = 1;
    buzzer_close_pend = 0;
    buzzer_open_acked = 0;

    /* PROD: buzzer trigger is always important */
    PROD_LOG(" [BUZZER] Door open >30s at %02d:%02d:%02d IST. TX now.\r\n",
             dt->tm_hour, dt->tm_min, dt->tm_sec);

    UTIL_SEQ_SetTask(
        (1 << CFG_SEQ_Task_LoRaSendOnTxTimerOrButtonEvent),
        CFG_SEQ_Prio_0);
}
