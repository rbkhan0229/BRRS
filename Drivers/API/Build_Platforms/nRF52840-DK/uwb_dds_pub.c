/*! ----------------------------------------------------------------------------
 *  @file    uwb_dds_pub.c
 *  @brief   Simple DDS Pub example code
 *
 * @author Decawave
 *
 * @copyright SPDX-FileCopyrightText: Copyright (c) 2024 Qorvo US, Inc.
 *            SPDX-License-Identifier: LicenseRef-QORVO-2
 *
 */

#include "deca_probe_interface.h"
#include <deca_device_api.h>
#include <deca_spi.h>
#include <example_selection.h>
#include <port.h>
#include <shared_defines.h>
#include <shared_functions.h>


#if defined(TEST_DDS_PUB)

extern void test_run_info(unsigned char *data);

/* Example application name */
#define APP_NAME "UWB DDS PUB TEST v1.0"

#define USE_SPI2 1 // set this to 1 to use DW37X0 SPI2

/* Default communication configuration. We use default non-STS DW mode. */
static dwt_config_t config = {
    5,                /* Channel number. */
    DWT_PLEN_128,     /* Preamble length. Used in TX only. */
    DWT_PAC8,         /* Preamble acquisition chunk size. Used in RX only. */
    9,                /* TX preamble code. Used in TX only. */
    9,                /* RX preamble code. Used in RX only. */
    1,//original #is 1                /* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol, 2 for non-standard 16 symbol SFD and 3 for 4z 8 symbol SDF type */
    DWT_BR_6M8,       /* Data rate. */
    DWT_PHRMODE_STD,  /* PHY header mode. */
    DWT_PHRRATE_STD,  /* PHY header rate. */
    (129 + 8 - 8),    /* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
    DWT_STS_MODE_OFF, /* No STS mode enabled (STS Mode 0). */
    DWT_STS_LEN_64,   /* STS length, see allowed values in Enum dwt_sts_lengths_e */
    DWT_PDOA_M0       /* PDOA mode off */
};

enum {
  IDX_FTYPE     = 0,  // 0: frame type
  IDX_SEQ       = 1,  // 1: seq
  IDX_DEV_ID    = 2,  // 2~9: device ID
  DEV_ID_LEN    = 8,
  TOPIC_ID_IDX  = 11,
  IDX_TOPIC_L   = 10, // 10: topic LSB
  IDX_TOPIC_H   = 11, // 11: topic MSB
  IDX_PAYLOAD   = 12, // 12~: payload
  TX_MSG_SIZE   = 120,
  READER_ENTITY = 0,
  WRITER_ENTITY = 1,
  MATCH_MSG_LEN = 12,
};

/* The frame sent in this example is an 802.15.4e standard blink. It is a 12-byte frame composed of the following fields:
 *     - byte 0: frame type (0xC5 for a blink).
 *     - byte 1: sequence number, incremented for each new frame.
 *     - byte 2 -> 9: device ID, see NOTE 1 below.
 */
//static uint8_t tx_msg[TX_MSG_SIZE] = {
//  0xC5,      // frame type (MAC Header)
//  0,         // seq (나중에 ++) (Mac Header)
//  'D','E','C','A','W','A','V','E', // device ID (MAC Header)
//  WRITER_ENTITY, // entityKind (0: reader, 1: writer)
//  1,       // topic placeholder
//  [12 ... TX_MSG_SIZE-1] = 0 // 나머지 0으로 초기화
//};

static uint8_t match_msg[MATCH_MSG_LEN] = {
    0xC5,
    0,
    'P','U','B','L','I','S','H','R',
    WRITER_ENTITY,
    1,
};

/* Index to access to sequence number of the blink frame in the tx_msg array. */
#define BLINK_FRAME_SN_IDX 1

#define FRAME_LENGTH (sizeof(tx_msg) + FCS_LEN) // The real length that is going to be transmitted

/* Inter-frame delay period, in milliseconds. */
#define TX_DELAY_US 200

/* Values for the PG_DELAY and TX_POWER registers reflect the bandwidth and power of the spectrum at the current
 * temperature. These values can be calibrated prior to taking reference measurements. See NOTE 2 below. */
extern dwt_txconfig_t txconfig_options;


/* ================================DDS================================ */

#define MAX_PARTICIPANTS 16

#define HT_SIZE           64   // 해시테이블 크기 (2^n 로 해두면 모듈로 빠름)

int serialize(const void *in,  uint8_t *out, size_t out_size) {
//TODO: -

}

int deserialize(const void *in,  size_t in_size, void *out) {
//TODO: -

}

typedef struct {
    uint8_t  key[DEV_ID_LEN];
    bool     occupied;
    bool     deleted;          // 삭제 마커(툼스톤)
} ht_entry_t;

typedef struct {
    ht_entry_t table[HT_SIZE];
    uint8_t    count;
} endpoint_ht_t;


/* QoS Level Custom */
typedef enum {
    QoS_BEST_EFFORT = 0, // Throughput max
    QoS_RELIABLE    = 1, // Reliability max (++ACK)
} qos_level_t;

/* Message Data (tx data) */
typedef struct {
  uint8_t              user_id;
  uint8_t             tx_msg[TX_MSG_SIZE];
} msg_data;

/* Type Support */
typedef struct {
    int  (*serialize)   (const void *in,  uint8_t *out, size_t out_size);
    int  (*deserialize) (const void *in,  size_t in_size, void *out);
    const char *name;
} type_support;


/* Endpoint */
typedef struct {
    uint8_t            participants[MAX_PARTICIPANTS][DEV_ID_LEN]; // [use deviceID instead of GUID]
    uint8_t            participants_count;
} endpoint_list_t;

/* Publisher */
typedef struct {
    endpoint_ht_t    discovered_writers; // publishers
    endpoint_ht_t    discovered_readers; // subscribers
    msg_data           tx_data;
} publisher;

/* Topic */
typedef struct {
    uint16_t           topic_id;
    type_support       type;
    const char*        name;         // "HelloWorldTopic", Topic name
    const char*        type_name;    // "demo::HelloWorld", TypeSupport name
    endpoint_ht_t      publishers; // publishers
    endpoint_ht_t      subscribers; // subscribers
    // qos_level_t        qos_type;  // e.g. RELIABLE or BEST_EFFORT
    // ... 기타 QoS 정책 (deadline, latency_budget, etc.)
    // x topic listener
} topic;


/* Init */
static topic my_topic = {
    .topic_id     = 1,
    .type         = { serialize, deserialize, "MJ" },
    .name         = "MJ_TOPIC_NAME",
    .type_name    = "MJ_TYPE_NAME",
    // 해시테이블은 모두 0으로 채워져 있으므로 .count = 0, .occupied = false, .deleted = false 자동 초기화
    .publishers   = { .count = 0 },
    .subscribers  = { .count = 0 },
};

static publisher my_publisher = {
    .discovered_writers = { .count = 0 },
    .discovered_readers = { .count = 0 },
    .tx_data = { 
        .user_id = 0,
        .tx_msg =  {
          0xC5,      // frame type (MAC Header)
          0,         // seq (나중에 ++) (Mac Header)
          'P','U','B','L','I','S','H','R', // device ID (MAC Header)
          WRITER_ENTITY, // entity type (0: reader, 1: writer)
          1,       // topic id
          [12 ... TX_MSG_SIZE-1] = 0 // 나머지 0으로 초기화
        } ,
    },
};

/* Methods */


// 단순 XOR 해시 함수
static inline uint32_t hash_dev_id(const uint8_t *id) {
    uint32_t h = 0;
    for (int i = 0; i < DEV_ID_LEN; ++i) h = (h << 5) ^ (h >> 27) ^ id[i];
    return h & (HT_SIZE - 1);
}

// ID 추가 (성공 true, 이미 있으면 false, 공간 없으면 false)
bool ht_add(endpoint_ht_t *ht, const uint8_t *id) {
    if (ht->count >= MAX_PARTICIPANTS) return false;
    uint32_t idx = hash_dev_id(id);
    for (int i = 0; i < HT_SIZE; ++i) {
        ht_entry_t *e = &ht->table[idx];
        if (!e->occupied) {
            // 비어있거나 툼스톤 자리면 삽입
            memcpy(e->key, id, DEV_ID_LEN);
            e->occupied = true;
            e->deleted  = false;
            ht->count++;
            return true;
        }
        if (!e->deleted && memcmp(e->key, id, DEV_ID_LEN) == 0) {
            // 이미 등록된 ID
            return false;
        }
        idx = (idx + 1) & (HT_SIZE - 1);
    }
    return false; // 테이블 full
}

// ID 삭제 (성공 true, 없으면 false)
bool ht_remove(endpoint_ht_t *ht, const uint8_t *id) {
    uint32_t idx = hash_dev_id(id);
    for (int i = 0; i < HT_SIZE; ++i) {
        ht_entry_t *e = &ht->table[idx];
        if (!e->occupied && !e->deleted) {
            // 탐색 중 빈칸 만나면 없음
            return false;
        }
        if (e->occupied && memcmp(e->key, id, DEV_ID_LEN) == 0) {
            // 삭제 표시
            e->occupied = false;
            e->deleted  = true;
            ht->count--;
            return true;
        }
        idx = (idx + 1) & (HT_SIZE - 1);
    }
    return false;
}

// ID 존재 여부 확인
bool ht_contains(const endpoint_ht_t *ht, const uint8_t *id) {
    uint32_t idx = hash_dev_id(id);
    for (int i = 0; i < HT_SIZE; ++i) {
        const ht_entry_t *e = &ht->table[idx];
        if (!e->occupied && !e->deleted) {
            return false;
        }
        if (e->occupied && memcmp(e->key, id, DEV_ID_LEN) == 0) {
            return true;
        }
        idx = (idx + 1) & (HT_SIZE - 1);
    }
    return false;
}

// 테이블 초기화
void ht_init(endpoint_ht_t *ht) {
    memset(ht, 0, sizeof(*ht));
}

static void print_endpoint_ht(
        const char*       label,
        const endpoint_ht_t* ht)
{
    printf("%s count = %u\n", label, ht->count);
    for (size_t i = 0; i < HT_SIZE; ++i)
    {
        const ht_entry_t* e = &ht->table[i];
        if (e->occupied)
        {
            printf("  [%02zu] ID = 0x", i);
            for (size_t j = 0; j < DEV_ID_LEN; ++j)
            {
                printf("%02X", e->key[j]);
            }
            printf("\n");
        }
    }
}

/**
 * topic 에 포함된 publishers/subscribers 를 모두 출력
 */
void print_topic_info(const topic* t)
{
    if (!t) return;
    printf("==== Topic '%s' (ID=%u) ====\n", t->name, t->topic_id);
    print_endpoint_ht("Publishers",  &t->publishers);
    print_endpoint_ht("Subscribers", &t->subscribers);
    printf("============================\n");
}

// Publish value
void write(msg_data *data)
{
    // UWB TX
    dwt_writetxdata(TX_MSG_SIZE, data->tx_msg, 0);
    dwt_writetxfctrl(TX_MSG_SIZE + FCS_LEN, 0, 0);
    dwt_starttx(DWT_START_TX_IMMEDIATE);
    // Wait for tx event to be completed
    waitforsysstatus(NULL, NULL, DWT_INT_TXFRS_BIT_MASK, 0);
    dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

    printf("Published message to topic %u, seq=%u\r\n",
           my_topic.topic_id, data->tx_msg[IDX_SEQ]);

    /* Execute a delay between transmissions. */
    nrf_delay_us(TX_DELAY_US);

    /* Increment the blink frame sequence number (modulo 256). */
    data->tx_msg[IDX_SEQ]++;
}

static uint8_t rx_buffer[FRAME_LEN_MAX];

/* ================================DDS END================================ */

/**
 * Application entry point.
 */
int uwb_dds_pub(void)
{
#if USE_SPI2
    uint8_t sema_res;
#endif
    uint32_t dev_id;

    /* Hold copy of status register state here for reference so that it can be examined at a debug breakpoint. */
    uint32_t status_reg;
    /* Hold copy of frame length of frame received (if good) so that it can be examined at a debug breakpoint. */
    uint32_t frame_len;


    /* Display application name on LCD. */
    test_run_info((unsigned char *)APP_NAME);

    /* Configure SPI rate, DW3000 supports up to 38 MHz */
    port_set_dw_ic_spi_fastrate();

    /* Reset DW IC */
    reset_DWIC(); /* Target specific drive of RSTn line into DW IC low for a period. */

    Sleep(2); // Time needed for DW3000 to start up (transition from INIT_RC to IDLE_RC, or could wait for SPIRDY event)

    /* Probe for the correct device driver. */
    dwt_probe((struct dwt_probe_s *)&dw3000_probe_interf);

    dev_id = dwt_readdevid();
    if (dev_id == (uint32_t)DWT_DW3720_PDOA_DEV_ID)
    {
        /* If host is using SPI 2 to connect to DW3000 the code in the USE_SPI2 above should be set to 1 */
#if USE_SPI2
        change_SPI(SPI_2);

        /* Configure SPI rate, DW3000 supports up to 38 MHz */
        port_set_dw_ic_spi_fastrate();

        /* Reset DW IC */
        reset_DWIC(); /* Target specific drive of RSTn line into DW IC low for a period. */

        Sleep(2); // Time needed for DW3000 to start up (transition from INIT_RC to IDLE_RC, or could wait for SPIRDY event)

        /* If host is using SPI 2 to connect to DW3000 the it needs to request access or force access */

        sema_res = dwt_ds_sema_status();

        if ((sema_res & (0x2)) == 0) // the SPI2 is free
        {
            dwt_ds_sema_request();
        }
        else
        {
            test_run_info((unsigned char *)"SPI2 IS NOT FREE"); // If SPI2 is not free the host can force access
            while (1) { };
        }
#endif
    }

    while (!dwt_checkidlerc()) /* Need to make sure DW IC is in IDLE_RC before proceeding */ { };

    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR)
    {
        test_run_info((unsigned char *)"INIT FAILED     ");
        while (1) { };
    }

    /* If host is using SPI 2 to connect to DW3000 then the GPIOs are used for SPI2 and LEDs functionality cannot be used */
#if USE_SPI2 == 0
    /* Enabling LEDs here for debug so that for each TX the D1 LED will flash on DW3000 red eval-shield boards. */
    dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);
#endif

    /* Configure DW IC. See NOTE 5 below. */
    /* if the dwt_configure returns DWT_ERROR either the PLL or RX calibration has failed the host should reset the device */
    if (dwt_configure(&config))
    {
        test_run_info((unsigned char *)"CONFIG FAILED     ");
        while (1) { };
    }

    /* Configure the TX spectrum parameters (power PG delay and PG Count) */
    dwt_configuretxrf(&txconfig_options);
    
    Sleep(10000);
    /* Loop forever sending frames periodically. */
    int total_send = 0;
    while (1)
    {
        
        /* ============Discovery phase============ */
        
        /* Clear local RX buffer to avoid having leftovers from previous receptions  This is not necessary but is included here to aid reading
         * the RX buffer.
         * This is a good place to put a breakpoint. Here (after first time through the loop) the local status register will be set for last event
         * and if a good receive has happened the data buffer will have the data in it, and frame_len will be set to the length of the RX frame. */
        memset(rx_buffer, 0, sizeof(rx_buffer));

        /* Activate reception immediately. See NOTE 2 below. */
        dwt_rxenable(DWT_START_RX_IMMEDIATE);

        /* Poll until a frame is properly received or an error/timeout occurs. See NOTE 3 below.
         * STATUS register is 5 bytes long but, as the event we are looking at is in the first byte of the register, we can use this simplest API
         * function to access it. */
        waitforsysstatus(&status_reg, NULL, (DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO |SYS_STATUS_ALL_RX_ERR), 0);

        if (status_reg & DWT_INT_RXFCG_BIT_MASK) {

            frame_len = dwt_getframelength(0);

            if (frame_len <= FRAME_LEN_MAX)
            {
                uint8_t mac_header_length = 10;
                uint8_t topic_id_len = 1;
                uint8_t total_len = mac_header_length + topic_id_len + 1;
                dwt_readrxdata(rx_buffer, total_len, 0); /* No need to read the FCS/CRC. */

                
                for (int i=0; i<total_len; i++) {
                    printf("%02X : ", rx_buffer[i]);
                }
                printf("\r\n");
                
                uint8_t entitiy_type = rx_buffer[10];
                uint8_t rx_topic_id = rx_buffer[TOPIC_ID_IDX];
                uint8_t device_id[DEV_ID_LEN];

                uint8_t to_publish_topic_id = my_publisher.tx_data.tx_msg[TOPIC_ID_IDX];

                if (rx_topic_id != to_publish_topic_id) {
                  printf("It is not my topic : %d my topic id is %d !", rx_topic_id, to_publish_topic_id);
                  continue;
                }

                

                // device_id 버퍼에 복사
                memcpy(device_id, &rx_buffer[IDX_DEV_ID], DEV_ID_LEN);

                if (entitiy_type == READER_ENTITY) {
                  // topic subscribers append
                  // 
                  //my_topic.publishers
                  ht_add(&my_topic.subscribers, device_id);
                  ht_add(&my_publisher.discovered_readers, device_id);
                  printf("Subscriber : %s matched !\n", device_id);

                  // Publisher --match response-> Subscriber
                  dwt_writetxdata(MATCH_MSG_LEN, match_msg, 0);
                  dwt_writetxfctrl(MATCH_MSG_LEN + FCS_LEN, 0, 0);
                  dwt_starttx(DWT_START_TX_IMMEDIATE);
                  // Wait for tx event to be completed
                  waitforsysstatus(NULL, NULL, DWT_INT_TXFRS_BIT_MASK, 0);
                  dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
                  
                  Sleep(10);
                } else {
                  // topic publishers append
                  ht_add(&my_topic.publishers, device_id);
                }

                print_topic_info(&my_topic);


            }
          
        }

        /* ============Discovery phase END============ */

        /* ============Publishing Value============ */
        
        //for (int i=0; i<10; i++) {
        //    write(&my_publisher.tx_data);
        //}

        /* ============Publishing Value END============ */
    }
}

#endif
/*****************************************************************************************************************************************************
 * NOTES:
 *
 * 1. The device ID is a hard coded constant in the blink to keep the example simple but for a real product every device should have a unique ID.
 *    For development purposes it is possible to generate a DW IC unique ID by combining the Lot ID & Part Number values programmed into the
 *    DW IC during its manufacture. However there is no guarantee this will not conflict with someone else's implementation. We recommended that
 *    customers buy a block of addresses from the IEEE Registration Authority for their production items. See "EUI" in the DW IC User Manual.
 * 2. In a real application, for optimum performance within regulatory limits, it may be necessary to set TX pulse bandwidth and TX power, (using
 *    the dwt_configuretxrf API call) to per device calibrated values saved in the target system or the DW IC OTP memory.
 * 3. dwt_writetxdata() takes the full size of tx_msg as a parameter but only copies (size - 2) bytes as the check-sum at the end of the frame is
 *    automatically appended by the DW IC. This means that our tx_msg could be two bytes shorter without losing any data (but the sizeof would not
 *    work anymore then as we would still have to indicate the full length of the frame to dwt_writetxdata()).
 * 4. We use polled mode of operation here to keep the example as simple as possible, but the TXFRS status event can be used to generate an interrupt.
 *    Please refer to DW IC User Manual for more details on "interrupts".
 * 5. Desired configuration by user may be different to the current programmed configuration. dwt_configure is called to set desired
 *    configuration.
 ****************************************************************************************************************************************************/
