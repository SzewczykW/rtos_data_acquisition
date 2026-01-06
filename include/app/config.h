/**
 * @file config.h
 * @brief System Configuration File
 * @author Wiktor Szewczyk
 * @author Patryk Madej
 */

#ifndef CONFIG_H
#define CONFIG_H

/*===========================================================================*/
/* Hardware Configuration                                                    */
/*===========================================================================*/

/**
 * @defgroup USART_Config USART Configuration
 * @{
 */
#define USART_BAUDRATE 115200 /**< USART baud rate */
/** @} */

/**
 * @defgroup ADC_Config ADC Configuration
 * @{
 */
#define ADC_CHANNEL_SELECT 0 /**< ADC channel to use (0-7, maps to P0.23-P1.31) */
/** @} */

#endif /* CONFIG_H */
