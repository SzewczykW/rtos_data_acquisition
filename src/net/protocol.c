/**
 * @file protocol.c
 * @brief Application layer protocol implementation
 * @author Wiktor Szewczyk
 * @author Patryk Madej
 */

#include "protocol.h"

#include "logger.h"

#include <string.h>

/** Sequence number counter */
static uint16_t sequence_counter = 0;

/** Module initialized flag */
static bool initialized = false;

/**
 * @brief Build packet header
 */
static void
build_header(protocol_header_t *header, protocol_msg_type_t type, uint16_t payload_len)
{
    header->magic = PROTOCOL_MAGIC;
    header->msg_type = type;
    header->sequence = sequence_counter++;
    header->payload_len = payload_len;
}

protocol_status_t protocol_init(void)
{
    sequence_counter = 0;
    initialized = true;
    LOG_DEBUG("Protocol module initialized");
    return PROTO_STATUS_OK;
}

protocol_status_t protocol_build_data_packet(
    uint8_t *buffer, size_t buffer_len, uint8_t channel, const uint16_t *samples,
    uint16_t sample_count, size_t *out_len
)
{
    if (buffer == NULL || samples == NULL || out_len == NULL)
    {
        return PROTO_STATUS_ERROR;
    }

    /* Calculate sizes */
    size_t payload_size =
        sizeof(protocol_data_payload_t) + (sample_count * sizeof(uint16_t));
    size_t total_size = sizeof(protocol_header_t) + payload_size;

    if (buffer_len < total_size)
    {
        return PROTO_STATUS_BUFFER_TOO_SMALL;
    }

    /* Build header */
    protocol_header_t *header = (protocol_header_t *)buffer;
    build_header(header, MSG_TYPE_DATA, (uint16_t)payload_size);

    /* Build payload */
    protocol_data_payload_t *payload =
        (protocol_data_payload_t *)(buffer + sizeof(protocol_header_t));

    payload->channel = channel;
    payload->reserved = 0;
    payload->sample_count = sample_count;

    /* Copy samples */
    memcpy(payload->samples, samples, sample_count * sizeof(uint16_t));

    *out_len = total_size;

    return PROTO_STATUS_OK;
}

protocol_status_t
protocol_build_ping(uint8_t *buffer, size_t buffer_len, size_t *out_len)
{
    if (buffer == NULL || out_len == NULL)
    {
        return PROTO_STATUS_ERROR;
    }

    size_t total_size = sizeof(protocol_header_t);
    if (buffer_len < total_size)
    {
        return PROTO_STATUS_BUFFER_TOO_SMALL;
    }

    protocol_header_t *header = (protocol_header_t *)buffer;
    build_header(header, MSG_TYPE_PING, 0);

    *out_len = total_size;

    return PROTO_STATUS_OK;
}

protocol_status_t
protocol_build_pong(uint8_t *buffer, size_t buffer_len, size_t *out_len)
{
    if (buffer == NULL || out_len == NULL)
    {
        return PROTO_STATUS_ERROR;
    }

    size_t total_size = sizeof(protocol_header_t);
    if (buffer_len < total_size)
    {
        return PROTO_STATUS_BUFFER_TOO_SMALL;
    }

    protocol_header_t *header = (protocol_header_t *)buffer;
    build_header(header, MSG_TYPE_PONG, 0);

    *out_len = total_size;

    return PROTO_STATUS_OK;
}

protocol_status_t protocol_build_status(
    uint8_t *buffer, size_t buffer_len, const protocol_status_payload_t *status,
    size_t *out_len
)
{
    if (buffer == NULL || status == NULL || out_len == NULL)
    {
        return PROTO_STATUS_ERROR;
    }

    size_t payload_size = sizeof(protocol_status_payload_t);
    size_t total_size = sizeof(protocol_header_t) + payload_size;

    if (buffer_len < total_size)
    {
        return PROTO_STATUS_BUFFER_TOO_SMALL;
    }

    protocol_header_t *header = (protocol_header_t *)buffer;
    build_header(header, MSG_TYPE_STATUS, (uint16_t)payload_size);

    memcpy(buffer + sizeof(protocol_header_t), status, payload_size);

    *out_len = total_size;

    return PROTO_STATUS_OK;
}

protocol_status_t protocol_parse_packet(
    const uint8_t *data, size_t len, protocol_header_t *header, const uint8_t **payload,
    size_t *payload_len
)
{
    if (data == NULL || header == NULL)
    {
        return PROTO_STATUS_ERROR;
    }

    /* Minimum packet size: header only (no CRC) */
    if (len < sizeof(protocol_header_t))
    {
        LOG_WARNING("Packet too short: %u bytes", len);
        return PROTO_STATUS_INVALID_MSG;
    }

    /* Copy header */
    memcpy(header, data, sizeof(protocol_header_t));

    /* Validate magic */
    if (header->magic != PROTOCOL_MAGIC)
    {
        LOG_WARNING("Invalid magic: 0x%04X", header->magic);
        return PROTO_STATUS_INVALID_MSG;
    }

    /* Validate payload length */
    size_t expected_len = sizeof(protocol_header_t) + header->payload_len;
    if (len < expected_len)
    {
        LOG_WARNING("Packet length mismatch: got %u, expected %u", len, expected_len);
        return PROTO_STATUS_INVALID_MSG;
    }

    /* Set payload pointer */
    if (payload != NULL && header->payload_len > 0)
    {
        *payload = data + sizeof(protocol_header_t);
    }
    else if (payload != NULL)
    {
        *payload = NULL;
    }

    if (payload_len != NULL)
    {
        *payload_len = header->payload_len;
    }

    return PROTO_STATUS_OK;
}

protocol_status_t protocol_parse_command(
    const uint8_t *payload, size_t payload_len, protocol_cmd_payload_t *cmd
)
{
    if (payload == NULL || cmd == NULL)
    {
        return PROTO_STATUS_ERROR;
    }

    if (payload_len < sizeof(protocol_cmd_payload_t))
    {
        return PROTO_STATUS_INVALID_MSG;
    }

    memcpy(cmd, payload, sizeof(protocol_cmd_payload_t));
    return PROTO_STATUS_OK;
}

uint16_t protocol_get_sequence(void)
{
    return sequence_counter;
}

void protocol_reset_sequence(void)
{
    sequence_counter = 0;
    LOG_DEBUG("Sequence counter reset");
}
