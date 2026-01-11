/**
 * @file udp_socket.c
 * @brief UDP Socket implementation using ARM CMSIS Network stack
 * @author Wiktor Szewczyk
 * @author Patryk Madej
 */

#include "udp_socket.h"

#include "Net_Config_UDP.h"
#include "cmsis_os2.h"
#include "logger.h"
#include "panic.h"
#include "rl_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Maximum receive buffer size in bytes */
#define UDP_RECV_BUFFER_SIZE UDP_MAX_PAYLOAD_SIZE

/** Socket state flags */
#define SOCKET_FLAG_USED     (1U << 0)
#define SOCKET_FLAG_BOUND    (1U << 1)
#define SOCKET_FLAG_CALLBACK (1U << 2)
#define SOCKET_FLAG_CLOSING  (1U << 3)

/* RX packet stored in queue (allocated from per-socket memory pool) */
typedef struct
{
    udp_endpoint_t remote;
    uint16_t       len;
    uint8_t        data[UDP_RECV_BUFFER_SIZE];
} udp_rx_pkt_t;

/* Special queue item meaning: socket is closing */
#define UDP_RX_PKT_CLOSING ((udp_rx_pkt_t *)(uintptr_t)1U)

/**
 * @brief Internal socket structure
 */
typedef struct udp_socket
{
    int32_t             net_socket;         /**< Network stack socket handle */
    uint16_t            local_port;         /**< Bound local port */
    uint8_t             flags;              /**< Socket state flags */
    udp_recv_callback_t callback;           /**< Receive callback */
    void               *callback_user_data; /**< User data for callback */
    uint8_t             recv_buffer[UDP_RECV_BUFFER_SIZE]; /**< Receive buffer */
    size_t              recv_len;                          /**< Received data length */
    udp_endpoint_t      recv_remote; /**< Remote endpoint of received data */
    osMessageQueueId_t  rx_queue;    /**< Queue of udp_rx_pkt_t* */
    osMemoryPoolId_t    rx_pool;     /**< Pool of udp_rx_pkt_t blocks */
    uint32_t            rx_dropped;  /**< Dropped RX packets */
} udp_socket_internal_t;

/** Socket pool */
static udp_socket_internal_t socket_pool[UDP_NUM_SOCKS];

/** Module initialized flag */
static bool module_initialized = false;

/** Mutex for socket pool access */
static osMutexId_t socket_mutex = NULL;

static volatile bool s_eth_link_known = false;
static volatile bool s_eth_link_up    = false;

void netETH_Notify(uint32_t if_num, netETH_Event event, uint32_t val)
{
    (void)val;

    if (if_num != 0U)
    {
        return;
    }

    switch (event)
    {
        case netETH_LinkUp:
            s_eth_link_known = true;
            s_eth_link_up    = true;
            break;

        case netETH_LinkDown:
            s_eth_link_known = true;
            s_eth_link_up    = false;
            break;

        default:
            break;
    }
}

/**
 * @brief Find socket by network handle
 */
static udp_socket_internal_t *find_socket_by_net_handle(int32_t net_socket)
{
    for (int i = 0; i < UDP_NUM_SOCKS; i++)
    {
        if ((socket_pool[i].flags & SOCKET_FLAG_USED) &&
            socket_pool[i].net_socket == net_socket)
        {
            return &socket_pool[i];
        }
    }
    return NULL;
}

/**
 * @brief Allocate socket from pool
 */
static udp_socket_internal_t *allocate_socket(void)
{
    for (int i = 0; i < UDP_NUM_SOCKS; i++)
    {
        if (!(socket_pool[i].flags & SOCKET_FLAG_USED))
        {
            memset(&socket_pool[i], 0, sizeof(udp_socket_internal_t));
            socket_pool[i].flags = SOCKET_FLAG_USED;
            return &socket_pool[i];
        }
    }

    return NULL;
}

/**
 * @brief Free socket back to pool
 */
static void free_socket(udp_socket_internal_t *sock)
{
    /* Wake any blocking receiver (best-effort) */
    if (sock->rx_queue != NULL)
    {
        udp_rx_pkt_t *closing = UDP_RX_PKT_CLOSING;
        (void)osMessageQueuePut(sock->rx_queue, &closing, 0U, 0U);
    }

    if (sock->rx_queue != NULL)
    {
        osMessageQueueDelete(sock->rx_queue);
        sock->rx_queue = NULL;
    }

    if (sock->rx_pool != NULL)
    {
        osMemoryPoolDelete(sock->rx_pool);
        sock->rx_pool = NULL;
    }

    memset(sock, 0, sizeof(udp_socket_internal_t));
}

/**
 * @brief Convert NET_ADDR to udp_endpoint_t
 */
static void net_addr_to_endpoint(const NET_ADDR *addr, udp_endpoint_t *endpoint)
{
    if (addr->addr_type == NET_ADDR_IP4)
    {
        memcpy(endpoint->ip.addr, addr->addr, 4);
        endpoint->port = addr->port;
    }
    else
    {
        /* IPv6 not supported in endpoint, use zeros */
        memset(&endpoint->ip, 0, sizeof(endpoint->ip));
        endpoint->port = addr->port;
    }
}

/**
 * @brief Convert udp_endpoint_t to NET_ADDR
 */
static void endpoint_to_net_addr(const udp_endpoint_t *endpoint, NET_ADDR *addr)
{
    addr->addr_type = NET_ADDR_IP4;
    addr->port      = endpoint->port;
    memcpy(addr->addr, endpoint->ip.addr, 4);
}

/**
 * @brief Network stack callback for received UDP data
 */
static uint32_t
udp_net_callback(int32_t socket, const NET_ADDR *addr, const uint8_t *buf, uint32_t len)
{
    LOG_DEBUG("Received UDP packet on socket %d, length %u", socket, len);
    udp_endpoint_t remote;
    net_addr_to_endpoint(addr, &remote);

    osMutexAcquire(socket_mutex, osWaitForever);

    udp_socket_internal_t *sock = find_socket_by_net_handle(socket);
    if (sock == NULL || !(sock->flags & SOCKET_FLAG_USED) ||
        (sock->flags & SOCKET_FLAG_CLOSING))
    {
        osMutexRelease(socket_mutex);
        return 0;
    }

    /* If user callback registered, call it WITHOUT holding mutex */
    if ((sock->flags & SOCKET_FLAG_CALLBACK) && (sock->callback != NULL))
    {
        udp_recv_callback_t cb = sock->callback;
        void               *ud = sock->callback_user_data;
        osMutexRelease(socket_mutex);

        cb((udp_socket_handle_t)sock, &remote, buf, (size_t)len, ud);
        return 1;
    }

    /* Blocking receive mode: queue packet */
    if (sock->rx_pool == NULL || sock->rx_queue == NULL)
    {
        osMutexRelease(socket_mutex);
        return 0;
    }

    udp_rx_pkt_t *pkt = (udp_rx_pkt_t *)osMemoryPoolAlloc(sock->rx_pool, 0U);
    if (pkt == NULL)
    {
        sock->rx_dropped++;
        osMutexRelease(socket_mutex);
        return 0;
    }

    pkt->remote       = remote;
    uint32_t copy_len = (len < UDP_RECV_BUFFER_SIZE) ? len : UDP_RECV_BUFFER_SIZE;
    pkt->len          = (uint16_t)copy_len;
    memcpy(pkt->data, buf, copy_len);

    udp_rx_pkt_t *msg = pkt;
    osStatus_t    qst = osMessageQueuePut(sock->rx_queue, &msg, 0U, 0U);
    if (qst != osOK)
    {
        (void)osMemoryPoolFree(sock->rx_pool, pkt);
        sock->rx_dropped++;
        osMutexRelease(socket_mutex);
        return 0;
    }

    osMutexRelease(socket_mutex);
    return 1;
}

udp_status_t udp_socket_init(void)
{
    if (module_initialized)
    {
        panic("UDP socket module already initialized", NULL);
        return UDP_STATUS_ALREADY_INIT;
    }

    const osMutexAttr_t mutex_attr = {
        .name      = "udp_mutex",
        .attr_bits = osMutexRecursive | osMutexPrioInherit,
        .cb_mem    = NULL,
        .cb_size   = 0
    };

    socket_mutex = osMutexNew(&mutex_attr);
    if (socket_mutex == NULL)
    {
        LOG_CRITICAL("Failed to create UDP socket mutex");
        panic("Failed to create UDP socket mutex", NULL);
        return UDP_STATUS_NO_MEMORY;
    }

    memset(socket_pool, 0, sizeof(socket_pool));

    module_initialized = true;
    return UDP_STATUS_OK;
}

udp_status_t udp_socket_deinit(void)
{
    if (!module_initialized)
    {
        LOG_WARNING("UDP socket module not initialized");
        return UDP_STATUS_NOT_INIT;
    }

    for (int i = 0; i < UDP_NUM_SOCKS; i++)
    {
        if (socket_pool[i].flags & SOCKET_FLAG_USED)
        {
            LOG_DEBUG("Closing UDP socket %d", i);
            udp_socket_close(&socket_pool[i]);
        }
    }

    if (socket_mutex != NULL)
    {
        LOG_DEBUG("Deleting UDP socket mutex");
        osMutexDelete(socket_mutex);
        socket_mutex = NULL;
    }

    module_initialized = false;
    LOG_INFO("UDP socket module deinitialized");

    return UDP_STATUS_OK;
}

udp_status_t udp_socket_create(udp_socket_handle_t *handle, uint16_t local_port)
{
    udp_status_t           result;
    udp_socket_internal_t *sock;
    netStatus              net_status;

    if (!module_initialized)
    {
        LOG_WARNING("UDP socket module not initialized");
        return UDP_STATUS_NOT_INIT;
    }

    if (handle == NULL)
    {
        LOG_CRITICAL("UDP socket handle not provided");
        return UDP_STATUS_INVALID_PARAM;
    }

    osMutexAcquire(socket_mutex, osWaitForever);

    sock = allocate_socket();
    if (sock == NULL)
    {
        LOG_ERROR("No free UDP sockets available");
        result = UDP_STATUS_NO_MEMORY;
        goto cleanup_mutex;
    }

    sock->net_socket = netUDP_GetSocket(udp_net_callback);
    if (sock->net_socket < 0)
    {
        LOG_ERROR("Failed to create network UDP socket");
        result = UDP_STATUS_NET_ERROR;
        goto cleanup_alloc;
    }

    net_status = netUDP_Open(sock->net_socket, local_port);
    if (net_status != netOK)
    {
        LOG_ERROR("Failed to bind UDP socket to port %u: %d", local_port, net_status);
        result = UDP_STATUS_NET_ERROR;
        goto cleanup_get_socket;
    }

    sock->rx_pool = osMemoryPoolNew(UDP_RX_QUEUE_LEN, sizeof(udp_rx_pkt_t), NULL);
    if (sock->rx_pool == NULL)
    {
        LOG_ERROR("Failed to create RX memory pool");
        result = UDP_STATUS_NO_MEMORY;
        goto cleanup_open;
    }

    sock->rx_queue = osMessageQueueNew(UDP_RX_QUEUE_LEN, sizeof(udp_rx_pkt_t *), NULL);
    if (sock->rx_queue == NULL)
    {
        LOG_ERROR("Failed to create RX message queue");
        result = UDP_STATUS_NO_MEMORY;
        goto cleanup_pool;
    }

    sock->local_port = local_port;
    sock->flags |= SOCKET_FLAG_BOUND;
    sock->recv_len = 0;

    *handle = sock;

    uint8_t ip_addr[NET_ADDR_IP4_LEN];
    netIF_GetOption(
        NET_IF_CLASS_ETH | 0, netIF_OptionIP4_Address, ip_addr, sizeof(ip_addr)
    );

    netARP_CacheIP(NET_IF_CLASS_ETH | 0, ip_addr, netARP_CacheFixedIP);
    osMutexRelease(socket_mutex);
    return UDP_STATUS_OK;

cleanup_pool:
    osMemoryPoolDelete(sock->rx_pool);
    sock->rx_pool = NULL;
cleanup_open:
    netUDP_Close(sock->net_socket);
cleanup_get_socket:
    netUDP_ReleaseSocket(sock->net_socket);
cleanup_alloc:
    free_socket(sock);
cleanup_mutex:
    osMutexRelease(socket_mutex);
    return result;
}

udp_status_t udp_socket_close(udp_socket_handle_t handle)
{
    if (!module_initialized)
    {
        LOG_WARNING("UDP socket module not initialized");
        return UDP_STATUS_NOT_INIT;
    }

    if (handle == NULL)
    {
        LOG_CRITICAL("UDP socket handle not provided");
        return UDP_STATUS_INVALID_PARAM;
    }

    osMutexAcquire(socket_mutex, osWaitForever);

    udp_socket_internal_t *sock = (udp_socket_internal_t *)handle;

    if (!(sock->flags & SOCKET_FLAG_USED))
    {
        LOG_WARNING("UDP socket handle not in use");
        osMutexRelease(socket_mutex);
        return UDP_STATUS_INVALID_PARAM;
    }

    sock->flags |= SOCKET_FLAG_CLOSING;
    if (sock->rx_queue != NULL)
    {
        LOG_DEBUG("Resetting RX message queue before closing socket");
        (void)osMessageQueueReset(sock->rx_queue);
        udp_rx_pkt_t *closing = UDP_RX_PKT_CLOSING;
        (void)osMessageQueuePut(sock->rx_queue, &closing, 0U, 0U);
    }

    netUDP_Close(sock->net_socket);
    netUDP_ReleaseSocket(sock->net_socket);

    uint16_t port = sock->local_port;
    free_socket(sock);

    osMutexRelease(socket_mutex);

    LOG_DEBUG("UDP socket on port %u closed", port);
    return UDP_STATUS_OK;
}

udp_status_t udp_socket_send(
    udp_socket_handle_t handle, const udp_endpoint_t *remote, const uint8_t *data,
    size_t len
)
{
    if (!module_initialized)
    {
        LOG_WARNING("UDP socket module not initialized");
        return UDP_STATUS_NOT_INIT;
    }

    if (handle == NULL || remote == NULL || data == NULL || len == 0)
    {
        LOG_CRITICAL("Invalid parameter(s) provided to udp_socket_send");
        return UDP_STATUS_INVALID_PARAM;
    }

    if (len > UDP_MAX_PAYLOAD_SIZE)
    {
        LOG_WARNING("UDP payload too large: %u > %u", len, UDP_MAX_PAYLOAD_SIZE);
        return UDP_STATUS_INVALID_PARAM;
    }

    udp_socket_internal_t *sock = (udp_socket_internal_t *)handle;

    if (!(sock->flags & SOCKET_FLAG_BOUND))
    {
        LOG_WARNING("UDP socket not bound");
        return UDP_STATUS_NOT_INIT;
    }

    if (!udp_socket_is_link_up())
    {
        LOG_WARNING("UDP link is down");
        return UDP_STATUS_LINK_DOWN;
    }

    NET_ADDR addr;
    endpoint_to_net_addr(remote, &addr);

    uint8_t *sendbuf = netUDP_GetBuffer(len);
    if (sendbuf == NULL)
    {
        LOG_ERROR("Failed to allocate UDP send buffer");
        return UDP_STATUS_NO_MEMORY;
    }

    memcpy(sendbuf, data, len);

    /* Send data - netUDP_Send takes buffer ownership */
    netStatus status = netUDP_Send(sock->net_socket, &addr, sendbuf, (uint32_t)len);
    if (status != netOK)
    {
        /* Buffer ownership transferred to stack even on error */
        LOG_ERROR("UDP send failed: %d", status);
        return UDP_STATUS_NET_ERROR;
    }

    return UDP_STATUS_OK;
}

udp_status_t udp_socket_sendto(
    udp_socket_handle_t handle, const char *ip_addr, uint16_t port, const uint8_t *data,
    size_t len
)
{
    udp_endpoint_t endpoint;
    udp_status_t   status;

    status = udp_endpoint_create(ip_addr, port, &endpoint);
    if (status != UDP_STATUS_OK)
    {
        LOG_CRITICAL("Invalid endpoint address: %s:%u", ip_addr, port);
        return status;
    }

    return udp_socket_send(handle, &endpoint, data, len);
}

udp_status_t udp_socket_recv(
    udp_socket_handle_t handle, udp_endpoint_t *remote, uint8_t *buffer,
    size_t buffer_len, size_t *received, uint32_t timeout_ms
)
{
    if (!module_initialized)
    {
        LOG_WARNING("UDP socket module not initialized");
        return UDP_STATUS_NOT_INIT;
    }

    if (handle == NULL || buffer == NULL || buffer_len == 0 || received == NULL)
    {
        LOG_CRITICAL("Invalid parameter(s) provided to udp_socket_recv");
        return UDP_STATUS_INVALID_PARAM;
    }

    udp_socket_internal_t *sock = (udp_socket_internal_t *)handle;

    if (!(sock->flags & SOCKET_FLAG_BOUND))
    {
        LOG_WARNING("UDP socket not bound");
        return UDP_STATUS_NOT_INIT;
    }

    *received = 0;

    if (sock->rx_queue == NULL)
    {
        LOG_WARNING("UDP socket receive queue is NULL");
        return UDP_STATUS_ERROR;
    }

    udp_rx_pkt_t *pkt       = NULL;
    osStatus_t    os_status = osMessageQueueGet(sock->rx_queue, &pkt, NULL, timeout_ms);

    if (os_status == osErrorTimeout)
    {
        LOG_DEBUG("UDP receive timeout after %u ms", timeout_ms);
        return UDP_STATUS_TIMEOUT;
    }
    if (os_status != osOK || pkt == NULL)
    {
        LOG_ERROR("UDP receive error: %d", os_status);
        return UDP_STATUS_ERROR;
    }

    if (pkt == UDP_RX_PKT_CLOSING)
    {
        LOG_DEBUG("UDP receive queue is closing");
        return UDP_STATUS_ERROR;
    }

    size_t copy_len = (pkt->len < buffer_len) ? (size_t)pkt->len : buffer_len;
    memcpy(buffer, pkt->data, copy_len);
    *received = copy_len;

    if (remote != NULL)
    {
        *remote = pkt->remote;
    }

    if (sock->rx_pool != NULL)
    {
        (void)osMemoryPoolFree(sock->rx_pool, pkt);
    }

    return UDP_STATUS_OK;
}

bool udp_socket_is_link_up(void)
{
    if (s_eth_link_known)
    {
        return s_eth_link_up;
    }

    uint8_t   ip_addr[NET_ADDR_IP4_LEN];
    netStatus status = netIF_GetOption(
        NET_IF_CLASS_ETH | 0, netIF_OptionIP4_Address, ip_addr, sizeof(ip_addr)
    );

    if (status == netOK)
    {
        return (ip_addr[0] | ip_addr[1] | ip_addr[2] | ip_addr[3]) != 0;
    }
    return false;
}

udp_status_t udp_socket_get_local_ip(udp_ipv4_addr_t *ip)
{
    if (ip == NULL)
    {
        return UDP_STATUS_INVALID_PARAM;
    }

    /* Get interface 0 local IP */
    uint8_t   ip_addr[NET_ADDR_IP4_LEN];
    netStatus status = netIF_GetOption(
        NET_IF_CLASS_ETH | 0, netIF_OptionIP4_Address, ip_addr, sizeof(ip_addr)
    );

    if (status != netOK)
    {
        return UDP_STATUS_NET_ERROR;
    }

    memcpy(ip->addr, ip_addr, 4);
    return UDP_STATUS_OK;
}

udp_status_t udp_ipv4_from_string(const char *ip_str, udp_ipv4_addr_t *ip)
{
    if (ip_str == NULL || ip == NULL)
    {
        return UDP_STATUS_INVALID_PARAM;
    }

    unsigned int a, b, c, d;
    if (sscanf(ip_str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4)
    {
        return UDP_STATUS_INVALID_PARAM;
    }

    if (a > 255 || b > 255 || c > 255 || d > 255)
    {
        return UDP_STATUS_INVALID_PARAM;
    }

    ip->addr[0] = (uint8_t)a;
    ip->addr[1] = (uint8_t)b;
    ip->addr[2] = (uint8_t)c;
    ip->addr[3] = (uint8_t)d;

    return UDP_STATUS_OK;
}

char *udp_ipv4_to_string(const udp_ipv4_addr_t *ip, char *buffer, size_t buffer_len)
{
    if (ip == NULL || buffer == NULL || buffer_len < 16)
    {
        return NULL;
    }

    snprintf(
        buffer, buffer_len, "%u.%u.%u.%u", ip->addr[0], ip->addr[1], ip->addr[2],
        ip->addr[3]
    );

    return buffer;
}

udp_status_t
udp_endpoint_create(const char *ip_str, uint16_t port, udp_endpoint_t *endpoint)
{
    if (endpoint == NULL)
    {
        return UDP_STATUS_INVALID_PARAM;
    }

    udp_status_t status = udp_ipv4_from_string(ip_str, &endpoint->ip);
    if (status != UDP_STATUS_OK)
    {
        return status;
    }

    endpoint->port = port;
    return UDP_STATUS_OK;
}
