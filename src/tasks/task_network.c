/**
 * @file task_network.c
 * @brief Network task implementation for UDP communication
 * @author Wiktor Szewczyk
 * @author Patryk Madej
 */

#include "task_network.h"

#include "cmsis_os2.h"
#include "logger.h"
#include "protocol.h"
#include "rl_net.h"
#include "task_acquisition.h"
#include "udp_socket.h"

#include <string.h>

/** Maximum packet buffer size in bytes */
#define PACKET_BUFFER_SIZE 1500
/** Link check interval in ms */
#define LINK_CHECK_INTERVAL 500
/** IP address wait timeout in ms */
#define IP_WAIT_TIMEOUT 30000

static osThreadId_t network_thread = NULL;
static const osThreadAttr_t network_thread_attr = {
    .name = "NetworkTask",
    .stack_size = TASK_NETWORK_STACK_SIZE * sizeof(uint32_t),
    .priority = TASK_NETWORK_PRIORITY,
};
static volatile network_state_t current_state = NET_STATE_INIT;
static network_stats_t stats = {0};
static udp_socket_handle_t udp_socket = NULL;
static udp_endpoint_t remote_target = {0};
static bool target_set_by_start = false;
static uint8_t tx_buffer[PACKET_BUFFER_SIZE];
static uint8_t rx_buffer[PACKET_BUFFER_SIZE];
static bool initialized = false;

/**
 * @brief Wait for Ethernet link to come up
 */
static bool wait_for_link(uint32_t timeout_ms)
{
    uint32_t start = osKernelGetTickCount();

    while ((osKernelGetTickCount() - start) < timeout_ms)
    {
        if (udp_socket_is_link_up())
        {
            return true;
        }
        osDelay(LINK_CHECK_INTERVAL);
    }

    return false;
}

/**
 * @brief Wait for valid IP address
 */
static bool wait_for_ip(uint32_t timeout_ms)
{
    uint32_t start = osKernelGetTickCount();
    udp_ipv4_addr_t ip;

    while ((osKernelGetTickCount() - start) < timeout_ms)
    {
        if (udp_socket_get_local_ip(&ip) == UDP_STATUS_OK)
        {
            if (ip.addr[0] != 0 || ip.addr[1] != 0 || ip.addr[2] != 0 ||
                ip.addr[3] != 0)
            {
                return true;
            }
        }
        osDelay(500);
    }

    return false;
}

/**
 * @brief Handle received command
 */
static void
handle_command(const protocol_cmd_payload_t *cmd, const udp_endpoint_t *remote)
{
    size_t response_len = 0;
    protocol_status_t status = PROTO_STATUS_ERROR;
    char ip_str[16];

    LOG_INFO(
        "Command received: 0x%02X, param_type: %u, param: %u", cmd->cmd,
        cmd->param_type, cmd->param
    );

    switch (cmd->cmd)
    {
        case CMD_GET_STATUS:
        {
            protocol_status_payload_t status_payload = {
                .acquiring = acquisition_is_running() ? 1 : 0,
                .channel = acquisition_get_channel(),
                .threshold_mv = acquisition_get_threshold_mv(),
                .uptime = osKernelGetTickCount() / 1000,
                .samples_sent = stats.packets_sent
            };

            status = protocol_build_status(
                tx_buffer, sizeof(tx_buffer), &status_payload, &response_len
            );
        }
        break;

        case CMD_START_ACQ:
            /* Set remote target to sender of START command */
            remote_target = *remote;
            target_set_by_start = true;
            udp_ipv4_to_string(&remote->ip, ip_str, sizeof(ip_str));
            LOG_INFO("Acquisition target set to %s:%u", ip_str, remote->port);

            if (acquisition_start() == 0)
            {
                LOG_INFO("Acquisition started");
            }
            else
            {
                LOG_ERROR("Failed to start acquisition");
            }
            /* No response - fire and forget */
            return;

        case CMD_STOP_ACQ:
            acquisition_stop();
            LOG_INFO("Acquisition stopped");
            return;

        case CMD_CONFIGURE:
            /* Handle configuration based on param_type */
            switch (cmd->param_type)
            {
                case CONFIG_THRESHOLD_PERCENT:
                    if (acquisition_set_threshold_percent((uint8_t)cmd->param) == 0)
                    {
                        LOG_INFO("Threshold set to %u%%", cmd->param);
                    }
                    break;

                case CONFIG_THRESHOLD_MV:
                    if (acquisition_set_threshold_mv(cmd->param) == 0)
                    {
                        LOG_INFO("Threshold set to %u mV", cmd->param);
                    }
                    break;

                case CONFIG_BATCH_SIZE:
                    if (acquisition_set_batch_size(cmd->param) == 0)
                    {
                        LOG_INFO("Batch size set to %u", cmd->param);
                    }
                    else
                    {
                        LOG_WARNING(
                            "Invalid batch size: %u (max %u)", cmd->param,
                            ACQUISITION_MAX_BATCH_SIZE
                        );
                    }
                    break;

                case CONFIG_CHANNEL:
                    if (acquisition_set_channel((adc_channel_t)cmd->param) == 0)
                    {
                        LOG_INFO("Channel set to %u", cmd->param);
                    }
                    break;

                case CONFIG_RESET_SEQUENCE:
                    protocol_reset_sequence();
                    LOG_INFO("Sequence counter reset");
                    break;

                case CONFIG_LOG_LEVEL:
                    if (cmd->param <= LOG_LEVEL_NONE)
                    {
                        logger_set_level((log_level_t)cmd->param);
                        LOG_INFO("Log level set to %u", cmd->param);
                    }
                    break;

                default:
                    LOG_WARNING("Unknown config param_type: %u", cmd->param_type);
                    break;
            }
            return;

        default:
            LOG_WARNING("Unknown command: 0x%02X", cmd->cmd);
            return;
    }

    if (status == PROTO_STATUS_OK && response_len > 0)
    {
        udp_socket_send(udp_socket, remote, tx_buffer, response_len);
        stats.packets_sent++;
        stats.bytes_sent += response_len;
    }
}

/**
 * @brief Process received UDP packet
 */
static void
process_received_packet(const uint8_t *data, size_t len, const udp_endpoint_t *remote)
{
    protocol_header_t header;
    const uint8_t *payload;
    size_t payload_len;

    char remote_ip[16];
    udp_ipv4_to_string(&remote->ip, remote_ip, sizeof(remote_ip));

    LOG_DEBUG("Received %u bytes from %s:%u", len, remote_ip, remote->port);

    protocol_status_t status =
        protocol_parse_packet(data, len, &header, &payload, &payload_len);

    if (status != PROTO_STATUS_OK)
    {
        LOG_WARNING(
            "Invalid packet from %s:%u (error %d)", remote_ip, remote->port, status
        );
        return;
    }

    switch (header.msg_type)
    {
        case MSG_TYPE_PING:
            LOG_DEBUG("Ping received, sending pong");
            {
                size_t pong_len;
                if (protocol_build_pong(tx_buffer, sizeof(tx_buffer), &pong_len) ==
                    PROTO_STATUS_OK)
                {
                    udp_socket_send(udp_socket, remote, tx_buffer, pong_len);
                    stats.packets_sent++;
                    stats.bytes_sent += pong_len;
                }
            }
            break;

        case MSG_TYPE_CMD:
            LOG_DEBUG("Command received");
            {
                protocol_cmd_payload_t cmd;
                if (protocol_parse_command(payload, payload_len, &cmd) ==
                    PROTO_STATUS_OK)
                {
                    handle_command(&cmd, remote);
                }
            }
            break;

        case MSG_TYPE_PONG:
            LOG_DEBUG("Pong received from %s:%u", remote_ip, remote->port);
            break;

        default:
            LOG_WARNING("Unknown message type: 0x%02X", header.msg_type);
            break;
    }
}

/**
 * @brief Main network task
 */
static void network_task(void *argument)
{
    (void)argument;

    current_state = NET_STATE_WAIT_LINK;
    LOG_INFO("Network task: waiting for Ethernet link...");

    if (!wait_for_link(30000))
    {
        LOG_ERROR("Ethernet link timeout");
        current_state = NET_STATE_ERROR;
        return;
    }

    LOG_INFO("Ethernet link up, waiting for IP address...");
    current_state = NET_STATE_WAIT_IP;

    if (!wait_for_ip(IP_WAIT_TIMEOUT))
    {
        LOG_ERROR("IP address timeout");
        current_state = NET_STATE_ERROR;
        return;
    }

    char ip_str[16];
    if (network_get_local_ip_str(ip_str, sizeof(ip_str)) != NULL)
    {
        LOG_INFO("IP address obtained: %s", ip_str);
    }

    udp_status_t status = udp_socket_create(&udp_socket, TASK_NETWORK_LOCAL_PORT);
    if (status != UDP_STATUS_OK)
    {
        LOG_ERROR("Failed to create UDP socket: %d", status);
        current_state = NET_STATE_ERROR;
        return;
    }

    LOG_INFO("UDP socket created on port %u", TASK_NETWORK_LOCAL_PORT);
    current_state = NET_STATE_READY;

    while (1)
    {
        if (!udp_socket_is_link_up())
        {
            LOG_WARNING("Ethernet link lost");
            current_state = NET_STATE_WAIT_LINK;

            if (!wait_for_link(30000))
            {
                current_state = NET_STATE_ERROR;
                continue;
            }

            current_state = NET_STATE_READY;
            LOG_INFO("Ethernet link restored");
        }

        udp_endpoint_t remote;
        size_t received;

        udp_status_t recv_status = udp_socket_recv(
            udp_socket, &remote, rx_buffer, sizeof(rx_buffer), &received, 100
        );

        if (recv_status == UDP_STATUS_OK && received > 0)
        {
            stats.packets_received++;
            stats.bytes_received += received;

            process_received_packet(rx_buffer, received, &remote);
        }
        else if (recv_status != UDP_STATUS_TIMEOUT)
        {
            stats.errors++;
        }

        osDelay(1);
    }
}

int network_init(void)
{
    if (initialized)
    {
        return 0;
    }
    LOG_INFO("Initializing network subsystem...");

    netStatus status = netInitialize();
    if (status != netOK)
    {
        LOG_ERROR("Network stack initialization failed: %d", status);
        return -1;
    }
    LOG_INFO("Network stack initialized");

    LOG_INFO("Initializing protocol module...");
    if (protocol_init() != PROTO_STATUS_OK)
    {
        LOG_ERROR("Protocol initialization failed");
        return -1;
    }
    LOG_INFO("Protocol module initialized");

    LOG_INFO("Initializing UDP socket...");
    if (udp_socket_init() != UDP_STATUS_OK)
    {
        LOG_ERROR("UDP socket module initialization failed");
        return -1;
    }
    LOG_INFO("UDP socket module initialized");
    initialized = true;
    LOG_INFO("Network subsystem initialized");

    return 0;
}

int network_task_start(void)
{
    if (!initialized)
    {
        LOG_ERROR("Network not initialized");
        return -1;
    }

    if (network_thread != NULL)
    {
        LOG_WARNING("Network task already running");
        return 0;
    }

    network_thread = osThreadNew(network_task, NULL, &network_thread_attr);
    if (network_thread == NULL)
    {
        LOG_ERROR("Failed to create network task");
        return -1;
    }

    LOG_INFO("Network task started");
    return 0;
}

network_state_t network_get_state(void)
{
    return current_state;
}

bool network_is_ready(void)
{
    return (current_state == NET_STATE_READY);
}

int network_set_target(const char *ip_addr, uint16_t port)
{
    if (ip_addr == NULL)
    {
        return -1;
    }

    udp_status_t status = udp_endpoint_create(ip_addr, port, &remote_target);
    if (status != UDP_STATUS_OK)
    {
        LOG_ERROR("Invalid target address: %s:%u", ip_addr, port);
        return -1;
    }

    LOG_INFO("Target set to %s:%u", ip_addr, port);
    return 0;
}

int network_send_data(uint8_t channel, const uint16_t *samples, uint16_t sample_count)
{
    if (!network_is_ready())
    {
        return -1;
    }

    if (samples == NULL || sample_count == 0)
    {
        return -1;
    }

    size_t packet_len;
    protocol_status_t proto_status = protocol_build_data_packet(
        tx_buffer, sizeof(tx_buffer), channel, samples, sample_count, &packet_len
    );

    if (proto_status != PROTO_STATUS_OK)
    {
        LOG_ERROR("Failed to build data packet: %d", proto_status);
        stats.errors++;
        return -1;
    }

    udp_status_t udp_status =
        udp_socket_send(udp_socket, &remote_target, tx_buffer, packet_len);

    if (udp_status != UDP_STATUS_OK)
    {
        LOG_ERROR("Failed to send data packet: %d", udp_status);
        stats.errors++;
        return -1;
    }

    stats.packets_sent++;
    stats.bytes_sent += packet_len;

    return 0;
}

int network_send_raw(const uint8_t *data, size_t len)
{
    if (!network_is_ready())
    {
        return -1;
    }

    if (data == NULL || len == 0)
    {
        return -1;
    }

    udp_status_t status = udp_socket_send(udp_socket, &remote_target, data, len);

    if (status != UDP_STATUS_OK)
    {
        stats.errors++;
        return -1;
    }

    stats.packets_sent++;
    stats.bytes_sent += len;

    return 0;
}

void network_get_stats(network_stats_t *out_stats)
{
    if (out_stats != NULL)
    {
        *out_stats = stats;
    }
}

char *network_get_local_ip_str(char *buffer, size_t buffer_len)
{
    if (buffer == NULL || buffer_len < 16)
    {
        return NULL;
    }

    udp_ipv4_addr_t ip;
    if (udp_socket_get_local_ip(&ip) != UDP_STATUS_OK)
    {
        return NULL;
    }

    return udp_ipv4_to_string(&ip, buffer, buffer_len);
}
