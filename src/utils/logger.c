/**
 * @file logger.c
 * @brief Logger module implementation using CMSIS USART Driver
 * @author Wiktor Szewczyk
 * @author Patryk Madej
 */

#include "logger.h"

#include "Driver_USART.h"
#include "FreeRTOSConfig.h"
#include "LPC17xx.h"
#include "RTE_Components.h"
#include "RTE_Device.h"
#include "cmsis_os2.h"
#include "config.h"
#include "panic.h"

#include <stdio.h>
#include <string.h>

/* DMA interrupt priority for FreeRTOS compatibility.
 * UART1 uses DMA (RTE_UART1_DMA_TX_EN=1), so TX complete callback
 * comes from DMA_IRQHandler, not UART1_IRQHandler.
 * Priority must be >= 16 to safely call osSemaphoreRelease from ISR.
 */

/* Auto-select USART driver + determine which IRQ delivers TX-complete callback
 * (DMA_IRQn when RTE_UARTx_DMA_TX_EN==1, otherwise UARTx_IRQn).
 */
#if defined(RTE_UART0) && (RTE_UART0 == 1)

extern ARM_DRIVER_USART Driver_USART0;
#define USART_DRIVER     (&Driver_USART0)
#define LOGGER_UART_IRQn UART0_IRQn
#if defined(RTE_UART0_DMA_TX_EN) && (RTE_UART0_DMA_TX_EN == 1)
#define LOGGER_TX_USES_DMA 1
#else
#define LOGGER_TX_USES_DMA 0
#endif

#elif defined(RTE_UART1) && (RTE_UART1 == 1)

extern ARM_DRIVER_USART Driver_USART1;
#define USART_DRIVER     (&Driver_USART1)
#define LOGGER_UART_IRQn UART1_IRQn
#if defined(RTE_UART1_DMA_TX_EN) && (RTE_UART1_DMA_TX_EN == 1)
#define LOGGER_TX_USES_DMA 1
#else
#define LOGGER_TX_USES_DMA 0
#endif

#elif defined(RTE_UART2) && (RTE_UART2 == 1)

extern ARM_DRIVER_USART Driver_USART2;
#define USART_DRIVER     (&Driver_USART2)
#define LOGGER_UART_IRQn UART2_IRQn
#if defined(RTE_UART2_DMA_TX_EN) && (RTE_UART2_DMA_TX_EN == 1)
#define LOGGER_TX_USES_DMA 1
#else
#define LOGGER_TX_USES_DMA 0
#endif

#elif defined(RTE_UART3) && (RTE_UART3 == 1)

extern ARM_DRIVER_USART Driver_USART3;
#define USART_DRIVER     (&Driver_USART3)
#define LOGGER_UART_IRQn UART3_IRQn
#if defined(RTE_UART3_DMA_TX_EN) && (RTE_UART3_DMA_TX_EN == 1)
#define LOGGER_TX_USES_DMA 1
#else
#define LOGGER_TX_USES_DMA 0
#endif

#else
#error "No USART enabled in RTE_Device.h. Enable RTE_UART0/1/2/3."
#endif

#if (LOGGER_TX_USES_DMA == 1)
#define LOGGER_TX_IRQn DMA_IRQn
#else
#define LOGGER_TX_IRQn LOGGER_UART_IRQn
#endif

#ifndef LOGGER_IRQ_PRIO
#define LOGGER_IRQ_PRIO                                                                \
    (configMAX_SYSCALL_INTERRUPT_PRIORITY >> (8U - __NVIC_PRIO_BITS))
#endif

#define LOGGER_BUFFER_SIZE      256  /**< Internal buffer size */
#define LOGGER_TX_TIMEOUT_MS    1000 /**< Default TX timeout */
#define LOGGER_MUTEX_TIMEOUT_MS 5000 /**< Mutex acquire timeout */

static log_level_t current_log_level = LOG_LEVEL_DEBUG;
static volatile uint8_t logger_initialized = 0;
static char log_buffer[LOGGER_BUFFER_SIZE];

static osMutexId_t logger_mutex = NULL;
static osSemaphoreId_t tx_semaphore = NULL;

static void usart_callback(uint32_t event);
static logger_status_t wait_for_tx_complete(uint32_t timeout_ms);

/**
 * @brief USART event callback
 * @param event USART event flags
 */
static void usart_callback(uint32_t event)
{
    uint32_t mask = ARM_USART_EVENT_RECEIVE_COMPLETE |
                    ARM_USART_EVENT_TRANSFER_COMPLETE | ARM_USART_EVENT_SEND_COMPLETE |
                    ARM_USART_EVENT_TX_COMPLETE;

    if (event & mask)
    {
        if (tx_semaphore != NULL)
        {
            osSemaphoreRelease(tx_semaphore);
        }
    }

    if (event & ARM_USART_EVENT_RX_TIMEOUT)
    {
        panic("USART RX timeout", NULL);
    }

    if (event & ARM_USART_EVENT_TX_UNDERFLOW)
    {
        panic("USART TX underflow", NULL);
    }
}

/**
 * @brief Wait for transmission to complete
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 * @return Logger status code
 */
static logger_status_t wait_for_tx_complete(uint32_t timeout_ms)
{
    osStatus_t status;

    if (tx_semaphore == NULL)
    {
        return LOGGER_ERROR_INIT;
    }

    status = osSemaphoreAcquire(tx_semaphore, timeout_ms);

    if (status == osOK)
    {
        return LOGGER_OK;
    }
    else if (status == osErrorTimeout)
    {
        return LOGGER_ERROR_BUSY;
    }
    else
    {
        return LOGGER_ERROR_SEND;
    }
}

/**
 * @brief Initialize logger with USART
 * @return Logger status code
 */
logger_status_t logger_init(void)
{
    int32_t status;

    if (logger_initialized)
    {
        return LOGGER_OK;
    }

    logger_mutex = osMutexNew(NULL);
    if (logger_mutex == NULL)
    {
        return LOGGER_ERROR_INIT;
    }

    tx_semaphore = osSemaphoreNew(1, 0, NULL);
    if (tx_semaphore == NULL)
    {
        osMutexDelete(logger_mutex);
        logger_mutex = NULL;
        return LOGGER_ERROR_INIT;
    }

    logger_status_t ret_status = LOGGER_ERROR_INIT;

    status = USART_DRIVER->Initialize(usart_callback);
    if (status != ARM_DRIVER_OK)
    {
        ret_status = LOGGER_ERROR_INIT;
        goto cleanup_rtos;
    }

    status = USART_DRIVER->PowerControl(ARM_POWER_FULL);
    if (status != ARM_DRIVER_OK)
    {
        ret_status = LOGGER_ERROR_POWER;
        goto cleanup_usart;
    }

    status = USART_DRIVER->Control(
        ARM_USART_MODE_ASYNCHRONOUS | ARM_USART_DATA_BITS_8 | ARM_USART_PARITY_NONE |
            ARM_USART_STOP_BITS_1 | ARM_USART_FLOW_CONTROL_NONE,
        USART_BAUDRATE
    );

    if (status != ARM_DRIVER_OK)
    {
        ret_status = LOGGER_ERROR_CONFIG;
        goto cleanup_power;
    }

    status = USART_DRIVER->Control(ARM_USART_CONTROL_TX, 1);
    if (status != ARM_DRIVER_OK)
    {
        ret_status = LOGGER_ERROR_CONFIG;
        goto cleanup_power;
    }

    NVIC_SetPriority(LOGGER_TX_IRQn, LOGGER_IRQ_PRIO);
    NVIC_SetPriority(LOGGER_UART_IRQn, LOGGER_IRQ_PRIO);

    logger_initialized = 1;
    return LOGGER_OK;

cleanup_power:
    USART_DRIVER->PowerControl(ARM_POWER_OFF);
cleanup_usart:
    USART_DRIVER->Uninitialize();
cleanup_rtos:
    osSemaphoreDelete(tx_semaphore);
    osMutexDelete(logger_mutex);
    tx_semaphore = NULL;
    logger_mutex = NULL;
    return ret_status;
}

/**
 * @brief Deinitialize logger
 * @return Logger status code
 */
logger_status_t logger_deinit(void)
{
    if (!logger_initialized)
    {
        return LOGGER_OK;
    }

    logger_flush(LOGGER_TX_TIMEOUT_MS);
    USART_DRIVER->Control(ARM_USART_CONTROL_TX, 0);
    USART_DRIVER->PowerControl(ARM_POWER_OFF);
    USART_DRIVER->Uninitialize();

    if (tx_semaphore != NULL)
    {
        osSemaphoreDelete(tx_semaphore);
        tx_semaphore = NULL;
    }

    if (logger_mutex != NULL)
    {
        osMutexDelete(logger_mutex);
        logger_mutex = NULL;
    }

    logger_initialized = 0;

    return LOGGER_OK;
}

/**
 * @brief Set current log level filter
 * @param level Minimum log level to display
 */
void logger_set_level(log_level_t level)
{
    if (level <= LOG_LEVEL_NONE)
    {
        current_log_level = level;
    }
}

/**
 * @brief Get current log level
 * @return Current log level
 */
log_level_t logger_get_level(void)
{
    return current_log_level;
}

/**
 * @brief Log a message with specified level
 * @param level Log level
 * @param format Format string (printf style)
 * @param ... Variable arguments
 * @return Number of characters written or negative error code
 */
int logger_log(log_level_t level, const char *format, ...)
{
    va_list args;
    int length;
    logger_status_t status;
    osStatus_t mutex_status;

    if (!logger_initialized)
    {
        return LOGGER_ERROR_INIT;
    }

    if (level < current_log_level || current_log_level == LOG_LEVEL_NONE)
    {
        return 0;
    }

    mutex_status = osMutexAcquire(logger_mutex, LOGGER_MUTEX_TIMEOUT_MS);
    if (mutex_status != osOK)
    {
        return LOGGER_ERROR_BUSY;
    }

    va_start(args, format);
    length = vsnprintf(log_buffer, LOGGER_BUFFER_SIZE, format, args);
    va_end(args);

    if (length < 0)
    {
        osMutexRelease(logger_mutex);
        return LOGGER_ERROR_PARAM;
    }

    /* Send message in chunks if too long */
    int total_sent = 0;
    int chunk_size = (length < LOGGER_BUFFER_SIZE) ? length : LOGGER_BUFFER_SIZE - 1;

    /* Send first chunk */
    status = logger_write_raw(log_buffer, chunk_size);
    if (status != LOGGER_OK)
    {
        osMutexRelease(logger_mutex);
        return status;
    }
    total_sent += chunk_size;

    /* Send truncation notice if message was too long */
    if (length >= LOGGER_BUFFER_SIZE)
    {
        const char *continuation = "...[TRUNCATED]...\r\n";
        status = logger_write_raw(continuation, strlen(continuation));
        if (status != LOGGER_OK)
        {
            osMutexRelease(logger_mutex);
            return status;
        }
    }

    osMutexRelease(logger_mutex);

    return total_sent;
}

/**
 * @brief Print raw data to USART (without level prefix)
 * @param data Pointer to data buffer
 * @param size Number of bytes to send
 * @return Logger status code
 */
logger_status_t logger_write_raw(const void *data, uint32_t size)
{
    int32_t status;
    logger_status_t wait_status;

    if (!logger_initialized)
    {
        return LOGGER_ERROR_INIT;
    }

    if (data == NULL || size == 0)
    {
        return LOGGER_ERROR_PARAM;
    }

    /* Start transmission - DMA will send data */
    status = USART_DRIVER->Send(data, size);
    if (status != ARM_DRIVER_OK)
    {
        return LOGGER_ERROR_SEND;
    }

    /* Wait for callback to signal TX complete - like reference osSignalWait pattern */
    wait_status = wait_for_tx_complete(LOGGER_TX_TIMEOUT_MS);
    if (wait_status != LOGGER_OK)
    {
        return wait_status;
    }

    return LOGGER_OK;
}

/**
 * @brief Wait for all pending transmissions to complete
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 * @return Logger status code
 */
logger_status_t logger_flush(uint32_t timeout_ms)
{
    if (!logger_initialized)
    {
        return LOGGER_ERROR_INIT;
    }

    return wait_for_tx_complete(timeout_ms);
}
