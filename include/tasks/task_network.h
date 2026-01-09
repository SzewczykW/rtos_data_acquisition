/**
 * @file task_network.h
 * @brief Network task for UDP communication handling
 * @author Wiktor Szewczyk
 * @author Patryk Madej
 */

#ifndef TASK_NETWORK_H
#define TASK_NETWORK_H

#include "cmsis_os2.h"
#include "protocol.h"
#include "udp_socket.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**< Stack size for the network task */
#define TASK_NETWORK_STACK_SIZE 4096
/**< Priority for the network task */
#define TASK_NETWORK_PRIORITY osPriorityNormal
/**< Local UDP port for the network task */
#define TASK_NETWORK_LOCAL_PORT 5000

    /**
     * @brief Network task state
     */
    typedef enum
    {
        NET_STATE_INIT      = 0, /**< Initializing */
        NET_STATE_WAIT_LINK = 1, /**< Waiting for Ethernet link */
        NET_STATE_WAIT_IP   = 2, /**< Waiting for IP address */
        NET_STATE_READY     = 3, /**< Ready to communicate */
        NET_STATE_ERROR     = 4  /**< Error state */
    } network_state_t;

    /**
     * @brief Network statistics
     */
    typedef struct
    {
        uint32_t packets_sent;     /**< Total packets sent */
        uint32_t packets_received; /**< Total packets received */
        uint32_t bytes_sent;       /**< Total bytes sent */
        uint32_t bytes_received;   /**< Total bytes received */
        uint32_t errors;           /**< Error count */
    } network_stats_t;

    /**
     * @brief Initialize network subsystem
     * @return 0 on success, negative on error
     */
    int network_init(void);

    /**
     * @brief Create and start network task
     * @return 0 on success, negative on error
     */
    int network_task_start(void);

    /**
     * @brief Get current network state
     * @return Current network state
     */
    network_state_t network_get_state(void);

    /**
     * @brief Check if network is ready for communication
     * @return true if ready
     */
    bool network_is_ready(void);

    /**
     * @brief Set remote target endpoint
     * @param ip_addr IP address string
     * @param port Port number
     * @return 0 on success
     */
    int network_set_target(const char *ip_addr, uint16_t port);

    /**
     * @brief Send ADC data packet to remote target
     * @param channel ADC channel
     * @param samples Sample array
     * @param sample_count Number of samples
     * @return 0 on success
     */
    int
    network_send_data(uint8_t channel, const uint16_t *samples, uint16_t sample_count);

    /**
     * @brief Send raw data to remote target
     * @param data Data buffer
     * @param len Data length
     * @return 0 on success
     */
    int network_send_raw(const uint8_t *data, size_t len);

    /**
     * @brief Get network statistics
     * @param stats Pointer to store statistics
     */
    void network_get_stats(network_stats_t *stats);

    /**
     * @brief Get local IP address as string
     * @param buffer Buffer to store IP string (min 16 bytes)
     * @param buffer_len Buffer length
     * @return Pointer to buffer on success, NULL on error
     */
    char *network_get_local_ip_str(char *buffer, size_t buffer_len);

#ifdef __cplusplus
}
#endif

#endif /* TASK_NETWORK_H */
