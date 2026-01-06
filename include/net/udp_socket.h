/**
 * @file udp_socket.h
 * @brief UDP Socket abstraction layer for network communication
 * @author Wiktor Szewczyk
 * @author Patryk Madej
 */

#ifndef UDP_SOCKET_H
#define UDP_SOCKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** Maximum UDP payload size in bytes */
#define UDP_MAX_PAYLOAD_SIZE 1472
/** Default receive timeout in milliseconds. Zero means no timeout */
#define UDP_DEFAULT_RECV_TIMEOUT 1000

    /**
     * @brief UDP socket status codes
     */
    typedef enum
    {
        UDP_STATUS_OK = 0,            /**< Operation successful */
        UDP_STATUS_ERROR = 1,         /**< General error */
        UDP_STATUS_TIMEOUT = 2,       /**< Operation timed out */
        UDP_STATUS_NO_MEMORY = 3,     /**< Memory allocation failed */
        UDP_STATUS_INVALID_PARAM = 4, /**< Invalid parameter */
        UDP_STATUS_NOT_INIT = 5,      /**< Socket not initialized */
        UDP_STATUS_ALREADY_INIT = 6,  /**< Socket already initialized */
        UDP_STATUS_NET_ERROR = 7,     /**< Network stack error */
        UDP_STATUS_LINK_DOWN = 8      /**< Ethernet link is down */
    } udp_status_t;

    /**
     * @brief IPv4 address structure
     */
    typedef struct
    {
        uint8_t addr[4]; /**< IPv4 address bytes in network order */
    } udp_ipv4_addr_t;

    /**
     * @brief UDP endpoint (address + port)
     */
    typedef struct
    {
        udp_ipv4_addr_t ip; /**< IPv4 address */
        uint16_t port;      /**< Port number in host order */
    } udp_endpoint_t;

    /**
     * @brief UDP socket handle
     */
    typedef struct udp_socket *udp_socket_handle_t;

    /**
     * @brief Callback function type for received data
     * @param handle Socket handle
     * @param remote Remote endpoint information
     * @param data Pointer to received data
     * @param len Length of received data
     * @param user_data User-provided context
     */
    typedef void (*udp_recv_callback_t)(
        udp_socket_handle_t handle, const udp_endpoint_t *remote, const uint8_t *data,
        size_t len, void *user_data
    );

    /**
     * @brief Initialize the UDP socket module
     * @note Must be called after network stack initialization
     * @return UDP_STATUS_OK on success
     */
    udp_status_t udp_socket_init(void);

    /**
     * @brief Deinitialize the UDP socket module
     * @return UDP_STATUS_OK on success
     */
    udp_status_t udp_socket_deinit(void);

    /**
     * @brief Create a new UDP socket
     * @param handle Pointer to store socket handle
     * @param local_port Local port to bind, 0 for auto-assign
     * @return UDP_STATUS_OK on success
     */
    udp_status_t udp_socket_create(udp_socket_handle_t *handle, uint16_t local_port);

    /**
     * @brief Close and destroy a UDP socket
     * @param handle Socket handle
     * @return UDP_STATUS_OK on success
     */
    udp_status_t udp_socket_close(udp_socket_handle_t handle);

    /**
     * @brief Send data to a remote endpoint
     * @param handle Socket handle
     * @param remote Remote endpoint
     * @param data Data to send
     * @param len Length of data
     * @return UDP_STATUS_OK on success
     */
    udp_status_t udp_socket_send(
        udp_socket_handle_t handle, const udp_endpoint_t *remote, const uint8_t *data,
        size_t len
    );

    /**
     * @brief Send data to a remote endpoint
     * @param handle Socket handle
     * @param ip_addr Remote IP address string
     * @param port Remote port number
     * @param data Data to send
     * @param len Length of data
     * @return UDP_STATUS_OK on success
     */
    udp_status_t udp_socket_sendto(
        udp_socket_handle_t handle, const char *ip_addr, uint16_t port,
        const uint8_t *data, size_t len
    );

    /**
     * @brief Receive data from socket with blocking timeout
     * @param handle Socket handle
     * @param remote Pointer to store remote endpoint info, can be NULL
     * @param buffer Buffer to store received data
     * @param buffer_len Size of buffer
     * @param received Pointer to store actual received length
     * @param timeout_ms Timeout in milliseconds. Zero means no wait, UINT32_MAX means
     * infinite
     * @return UDP_STATUS_OK on success, UDP_STATUS_TIMEOUT if no data
     */
    udp_status_t udp_socket_recv(
        udp_socket_handle_t handle, udp_endpoint_t *remote, uint8_t *buffer,
        size_t buffer_len, size_t *received, uint32_t timeout_ms
    );

    /**
     * @brief Register callback for incoming data
     * @param handle Socket handle
     * @param callback Callback function
     * @param user_data User context passed to callback
     * @return UDP_STATUS_OK on success
     */
    udp_status_t udp_socket_set_callback(
        udp_socket_handle_t handle, udp_recv_callback_t callback, void *user_data
    );

    /**
     * @brief Check if Ethernet link is up
     * @return true if link is up, false otherwise
     */
    bool udp_socket_is_link_up(void);

    /**
     * @brief Get local IP address
     * @param ip Pointer to store IP address
     * @return UDP_STATUS_OK on success
     */
    udp_status_t udp_socket_get_local_ip(udp_ipv4_addr_t *ip);

    /**
     * @brief Convert IP address string to udp_ipv4_addr_t
     * @param ip_str IP address string like "192.168.0.1"
     * @param ip Pointer to store converted address
     * @return UDP_STATUS_OK on success
     */
    udp_status_t udp_ipv4_from_string(const char *ip_str, udp_ipv4_addr_t *ip);

    /**
     * @brief Convert udp_ipv4_addr_t to string
     * @param ip IP address structure
     * @param buffer Buffer to store string, minimum 16 bytes
     * @param buffer_len Size of buffer
     * @return Pointer to buffer on success, NULL on error
     */
    char *
    udp_ipv4_to_string(const udp_ipv4_addr_t *ip, char *buffer, size_t buffer_len);

    /**
     * @brief Create endpoint from IP string and port
     * @param ip_str IP address string
     * @param port Port number
     * @param endpoint Pointer to store endpoint
     * @return UDP_STATUS_OK on success
     */
    udp_status_t
    udp_endpoint_create(const char *ip_str, uint16_t port, udp_endpoint_t *endpoint);

#ifdef __cplusplus
}
#endif

#endif /* UDP_SOCKET_H */
