/**
 * @file logger.h
 * @brief Logger module using CMSIS USART Driver
 * @author Wiktor Szewczyk
 * @author Patryk Madej
 */

/**
 * @defgroup Logger Logger
 * @{
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <stdarg.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** USART baud rate for logger */
#define USART_BAUDRATE 115200

    /**
     * @brief Log level enumeration
     */
    typedef enum
    {
        LOG_LEVEL_DEBUG    = 0, /**< Debug level messages */
        LOG_LEVEL_INFO     = 1, /**< Informational messages */
        LOG_LEVEL_WARNING  = 2, /**< Warning messages */
        LOG_LEVEL_ERROR    = 3, /**< Error messages */
        LOG_LEVEL_CRITICAL = 4, /**< Critical error messages */
        LOG_LEVEL_NONE     = 5  /**< Disable all logging */
    } log_level_t;

    /**
     * @brief Logger status codes
     */
    typedef enum
    {
        LOGGER_OK            = 0,  /**< Operation successful */
        LOGGER_ERROR_INIT    = -1, /**< Initialization error */
        LOGGER_ERROR_POWER   = -2, /**< Power control error */
        LOGGER_ERROR_CONFIG  = -3, /**< Configuration error */
        LOGGER_ERROR_SEND    = -4, /**< Send error */
        LOGGER_ERROR_BUSY    = -5, /**< Logger is busy */
        LOGGER_ERROR_PARAM   = -6, /**< Invalid parameter */
        LOGGER_ERROR_UNKNOWN = -7  /**< Unknown error */
    } logger_status_t;

    /**
     * @brief Initialize logger with USART
     * @return Logger status code
     */
    logger_status_t logger_init(void);

    /**
     * @brief Deinitialize logger
     * @return Logger status code
     */
    logger_status_t logger_deinit(void);

    /**
     * @brief Set current log level filter
     * @param level Minimum log level to display
     */
    void logger_set_level(log_level_t level);

    /**
     * @brief Get current log level
     * @return Current log level
     */
    log_level_t logger_get_level(void);

    /**
     * @brief Log a message with specified level
     * @param level Log level
     * @param format Format string (printf style)
     * @param ... Variable arguments
     * @return Number of characters written or negative error code
     */
    int logger_log(log_level_t level, const char *format, ...);

/**
 * @brief Log a debug message
 * @param format Format string (printf style)
 * @param ... Variable arguments
 */
#define LOG_DEBUG(format, ...)                                                         \
    logger_log(LOG_LEVEL_DEBUG, "[DEBUG] " format "\r\n", ##__VA_ARGS__)

/**
 * @brief Log an info message
 * @param format Format string (printf style)
 * @param ... Variable arguments
 */
#define LOG_INFO(format, ...)                                                          \
    logger_log(LOG_LEVEL_INFO, "[INFO] " format "\r\n", ##__VA_ARGS__)

/**
 * @brief Log a warning message
 * @param format Format string (printf style)
 * @param ... Variable arguments
 */
#define LOG_WARNING(format, ...)                                                       \
    logger_log(LOG_LEVEL_WARNING, "[WARN] " format "\r\n", ##__VA_ARGS__)

/**
 * @brief Log an error message
 * @param format Format string (printf style)
 * @param ... Variable arguments
 */
#define LOG_ERROR(format, ...)                                                         \
    logger_log(LOG_LEVEL_ERROR, "[ERROR] " format "\r\n", ##__VA_ARGS__)

/**
 * @brief Log a critical message
 * @param format Format string (printf style)
 * @param ... Variable arguments
 */
#define LOG_CRITICAL(format, ...)                                                      \
    logger_log(LOG_LEVEL_CRITICAL, "[CRIT] " format "\r\n", ##__VA_ARGS__)

    /**
     * @brief Print raw data to USART (without level prefix)
     * @param data Pointer to data buffer
     * @param size Number of bytes to send
     * @return Logger status code
     */
    logger_status_t logger_write_raw(const void *data, uint32_t size);

    /**
     * @brief Wait for all pending transmissions to complete
     * @param timeout_ms Timeout in milliseconds (0 = no timeout)
     * @return Logger status code
     */
    logger_status_t logger_flush(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* LOGGER_H */

/** End of Logger group */
/** @} */
