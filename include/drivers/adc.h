/**
 * @file adc.h
 * @brief ADC Driver for LPC1768
 * @author Wiktor Szewczyk
 * @author Patryk Madej
 * @warning This driver is NOT thread-safe. Caller must provide synchronization if
 * accessed from multiple contexts.
 */

#ifndef ADC_H
#define ADC_H

#include <stdint.h>

/**
 * @defgroup ADC_Driver ADC Driver
 * @{
 */
#define ADC_RESOLUTION 12U

/** ADC channel definitions */
typedef enum
{
    ADC_CHANNEL_0 = 0, /**< AD0.0 - P0_23 */
    ADC_CHANNEL_1 = 1, /**< AD0.1 - P0_24 */
    ADC_CHANNEL_2 = 2, /**< AD0.2 - P0_25 */
    ADC_CHANNEL_3 = 3, /**< AD0.3 - P0_26 */
    ADC_CHANNEL_4 = 4, /**< AD0.4 - P1_30 */
    ADC_CHANNEL_5 = 5, /**< AD0.5 - P1_31 */
    ADC_CHANNEL_6 = 6, /**< AD0.6 - P0_3  */
    ADC_CHANNEL_7 = 7, /**< AD0.7 - P0_2  */
    ADC_CHANNEL_MAX
} adc_channel_t;

/** ADC status codes */
typedef enum
{
    ADC_OK = 0,            /**< Success */
    ADC_ERROR_INIT = -1,   /**< Initialization error */
    ADC_ERROR_BUSY = -2,   /**< Conversion in progress */
    ADC_ERROR_PARAM = -3,  /**< Invalid parameter */
    ADC_ERROR_TIMEOUT = -4 /**< Timeout waiting for conversion */
} adc_status_t;

/**
 * @brief Initialize ADC peripheral
 * @param channel ADC channel to use
 * @return ADC status code
 */
adc_status_t adc_init(adc_channel_t channel);

/**
 * @brief Deinitialize ADC peripheral
 * @return ADC status code
 */
adc_status_t adc_deinit(void);

/**
 * @brief Start ADC conversion (non-blocking)
 * @return ADC status code
 */
adc_status_t adc_start_conversion(void);

/**
 * @brief Check if conversion is done
 * @return 1 if done, 0 if in progress
 */
int adc_conversion_done(void);

/**
 * @brief Get last converted value (non-blocking)
 * @param value Pointer to store converted value (12-bit)
 * @return ADC status code (ADC_ERROR_BUSY if conversion not done)
 */
adc_status_t adc_get_value(uint16_t *value);

/**
 * @brief Read ADC value synchronously (start + busy-wait)
 * @param value Pointer to store converted value (12-bit)
 * @return ADC status code
 */
adc_status_t adc_read_sync(uint16_t *value);

/** @} */

#endif /* ADC_H */
