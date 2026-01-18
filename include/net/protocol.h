/**
 * @file protocol.h
 * @brief Application layer protocol for data acquisition over UDP
 * @author Wiktor Szewczyk
 * @author Patryk Madej
 */

/**
 * @verbatim
 * PROTOCOL HEADER (7 bytes)
 * +--------+--------+--------+--------+--------+--------+--------+
 * |    MAGIC (2B)   |MSG_TYPE| SEQUENCE (2B)   |PAYLOAD_LEN (2B) |
 * +--------+--------+--------+--------+--------+--------+--------+
 * |  0xDA  |  0x7A  |  type  | seq_lo | seq_hi | len_lo | len_hi |
 * +--------+--------+--------+--------+--------+--------+--------+
 *    Byte 0    1        2        3        4        5        6
 *
 * DATA PACKET (MSG_TYPE = 0x10)
 * +--------+--------+--------+--------+--------+--------+--------+---
 * |      HEADER (7B)        |CHANNEL |RESERVED|SAMPLE_CNT (2B)  | ...
 * +--------+--------+--------+--------+--------+--------+--------+---
 * |                         |   ch   |  0x00  | cnt_lo | cnt_hi | samples[]
 * +--------+--------+--------+--------+--------+--------+--------+---
 *                              +7       +8       +9       +10      +11...
 *
 * SAMPLES ARRAY (each sample 2 bytes, little-endian)
 * +--------+--------+--------+--------+--------+--------+
 * | sample[0] (2B)  | sample[1] (2B)  | sample[N] (2B)  |
 * +--------+--------+--------+--------+--------+--------+
 *
 * COMMAND PACKET (MSG_TYPE = 0x20)
 * +--------+--------+--------+--------+--------+--------+--------+
 * |      HEADER (7B)        |  CMD   |PARAM_T |   PARAM (2B)    |
 * +--------+--------+--------+--------+--------+--------+--------+
 * |                         |  cmd   |  type  | prm_lo | prm_hi |
 * +--------+--------+--------+--------+--------+--------+--------+
 *                              +7       +8       +9       +10
 *
 * STATUS PACKET (MSG_TYPE = 0x30)
 * +--------+--------+--------+--------+--------+--------+--------+
 * |      HEADER (7B)        |  ACQ   |   CH   | THRESH_MV (2B)  |
 * +--------+--------+--------+--------+--------+--------+--------+
 * |                         | 0/1    |  ch    | thr_lo | thr_hi |
 * +--------+--------+--------+--------+--------+--------+--------+
 *                              +7       +8       +9       +10
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 * |        UPTIME (4B)                |      SAMPLES_SENT (4B)            |
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 * | upt[0] | upt[1] | upt[2] | upt[3] | smp[0] | smp[1] | smp[2] | smp[3] |
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 *   +11      +12      +13      +14      +15      +16      +17      +18
 *
 * PING/PONG PACKET (MSG_TYPE = 0x01 / 0x02)
 * +--------+--------+--------+--------+--------+--------+--------+
 * |      HEADER (7B)        |           (no payload)            |
 * +--------+--------+--------+--------+--------+--------+--------+
 * @endverbatim
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "udp_socket.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** Maximum data payload size */
#define PROTOCOL_MAX_DATA_SIZE 1400
/** Protocol magic number for packet identification */
#define PROTOCOL_MAGIC 0xDA7A

    /**
     * @brief Protocol message types
     */
    typedef enum
    {
        MSG_TYPE_PING   = 0x01, /**< Ping request */
        MSG_TYPE_PONG   = 0x02, /**< Pong response */
        MSG_TYPE_DATA   = 0x10, /**< ADC data packet */
        MSG_TYPE_CMD    = 0x20, /**< Command from host */
        MSG_TYPE_STATUS = 0x30  /**< Status report */
    } protocol_msg_type_t;

    /**
     * @brief Command codes
     */
    typedef enum
    {
        CMD_START_ACQ  = 0x01, /**< Start data acquisition */
        CMD_STOP_ACQ   = 0x02, /**< Stop data acquisition */
        CMD_GET_STATUS = 0x03, /**< Request status */
        CMD_CONFIGURE  = 0x04  /**< Configure measurement parameters */
    } protocol_cmd_t;

    /**
     * @brief Protocol status codes
     */
    typedef enum
    {
        PROTO_STATUS_OK = 0,
        PROTO_STATUS_ERROR,
        PROTO_STATUS_INVALID_MSG,
        PROTO_STATUS_BUFFER_TOO_SMALL
    } protocol_status_t;

    /**
     * @brief Protocol header (7 bytes)
     */
    typedef struct __attribute__((packed))
    {
        uint16_t magic;       /**< Magic number (PROTOCOL_MAGIC) */
        uint8_t  msg_type;    /**< Message type */
        uint16_t sequence;    /**< Sequence number */
        uint16_t payload_len; /**< Payload length */
    } protocol_header_t;

    /**
     * @brief ADC data payload (raw samples)
     */
    typedef struct __attribute__((packed))
    {
        uint8_t  channel;      /**< ADC channel */
        uint8_t  reserved;     /**< Reserved for alignment */
        uint16_t sample_count; /**< Number of samples */
        uint16_t samples[];    /**< ADC samples (flexible array) */
    } protocol_data_payload_t;

    /**
     * @brief Configuration parameter types for CMD_CONFIGURE
     */
    typedef enum
    {
        CONFIG_THRESHOLD_PERCENT = 0, /**< Threshold as percentage (0-100) */
        CONFIG_THRESHOLD_MV      = 1, /**< Threshold in millivolts (0-3300) */
        CONFIG_BATCH_SIZE        = 2, /**< Batch size, samples per packet, see
                                            TASK_ACQUISITION_MAX_BATCH_SIZE */
        CONFIG_CHANNEL        = 3,    /**< ADC channel (0-7) */
        CONFIG_RESET_SEQUENCE = 4,    /**< Reset sequence counter (param ignored) */
        CONFIG_LOG_LEVEL      = 5     /**< Set log level (0=DEBUG..5=NONE) */
    } protocol_config_param_t;

    /**
     * @brief Command payload
     */
    typedef struct __attribute__((packed))
    {
        uint8_t  cmd;        /**< Command code */
        uint8_t  param_type; /**< Parameter type (protocol_config_param_t) */
        uint16_t param;      /**< Command parameter value */
    } protocol_cmd_payload_t;

    /**
     * @brief Status payload
     */
    typedef struct __attribute__((packed))
    {
        uint8_t  acquiring;    /**< Acquisition active flag */
        uint8_t  channel;      /**< Current ADC channel */
        uint16_t threshold_mv; /**< Current threshold in millivolts */
        uint32_t uptime;       /**< System uptime in seconds */
        uint32_t samples_sent; /**< Total samples sent */
    } protocol_status_payload_t;

    /**
     * @brief Complete packet structure
     */
    typedef struct __attribute__((packed))
    {
        protocol_header_t header;
        uint8_t           payload[PROTOCOL_MAX_DATA_SIZE];
    } protocol_packet_t;

    /**
     * @brief Initialize protocol module
     * @return PROTO_STATUS_OK on success
     */
    protocol_status_t protocol_init(void);

    /**
     * @brief Build a data packet with ADC samples
     * @param buffer Output buffer
     * @param buffer_len Buffer size
     * @param channel ADC channel
     * @param samples Array of samples
     * @param sample_count Number of samples
     * @param out_len Pointer to store actual packet length
     * @return PROTO_STATUS_OK on success
     */
    protocol_status_t protocol_build_data_packet(
        uint8_t *buffer, size_t buffer_len, uint8_t channel, const uint16_t *samples,
        uint16_t sample_count, size_t *out_len
    );

    /**
     * @brief Build a ping packet
     * @param buffer Output buffer
     * @param buffer_len Buffer size
     * @param out_len Pointer to store actual packet length
     * @return PROTO_STATUS_OK on success
     */
    protocol_status_t
    protocol_build_ping(uint8_t *buffer, size_t buffer_len, size_t *out_len);

    /**
     * @brief Build a pong response packet
     * @param buffer Output buffer
     * @param buffer_len Buffer size
     * @param out_len Pointer to store actual packet length
     * @return PROTO_STATUS_OK on success
     */
    protocol_status_t
    protocol_build_pong(uint8_t *buffer, size_t buffer_len, size_t *out_len);

    /**
     * @brief Build a status packet
     * @param buffer Output buffer
     * @param buffer_len Buffer size
     * @param status Status information
     * @param out_len Pointer to store actual packet length
     * @return PROTO_STATUS_OK on success
     */
    protocol_status_t protocol_build_status(
        uint8_t *buffer, size_t buffer_len, const protocol_status_payload_t *status,
        size_t *out_len
    );

    /**
     * @brief Parse a received packet
     * @param data Received data
     * @param len Data length
     * @param header Pointer to store parsed header
     * @param payload Pointer to payload start (output)
     * @param payload_len Pointer to store payload length
     * @return PROTO_STATUS_OK if packet is valid
     */
    protocol_status_t protocol_parse_packet(
        const uint8_t *data, size_t len, protocol_header_t *header,
        const uint8_t **payload, size_t *payload_len
    );

    /**
     * @brief Parse command payload
     * @param payload Payload data
     * @param payload_len Payload length
     * @param cmd Pointer to store parsed command
     * @return PROTO_STATUS_OK on success
     */
    protocol_status_t protocol_parse_command(
        const uint8_t *payload, size_t payload_len, protocol_cmd_payload_t *cmd
    );

    /**
     * @brief Get current sequence number
     * @return Current sequence number
     */
    uint16_t protocol_get_sequence(void);

    /**
     * @brief Reset sequence counter to zero
     */
    void protocol_reset_sequence(void);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_H */
