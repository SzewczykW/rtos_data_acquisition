/**
 * @file system.c
 * @brief System initialization and FreeRTOS hooks implementation
 * @author Wiktor Szewczyk
 * @author Patryk Madej
 */

#include "system.h"

#include "LPC17xx.h"
#include "adc.h"
#include "config.h"
#include "logger.h"
#include "panic.h"

#include <stddef.h>

/**
 * @brief Initialize system before starting FreeRTOS scheduler
 * @return 0 on success, negative on error
 */
int system_init(void)
{
    if (logger_init() != LOGGER_OK)
    {
        return -1;
    }

    if (adc_init(ADC_CHANNEL_SELECT) != ADC_OK)
    {
        return -1;
    }

    return 0;
}

/**
 * @brief Stack overflow hook (required when configCHECK_FOR_STACK_OVERFLOW > 0)
 * @param xTask Handle of task with stack overflow
 * @param pcTaskName Name of task with stack overflow
 */
void vApplicationStackOverflowHook(void *xTask, char *pcTaskName)
{
    (void)xTask;

    panic("Stack overflow", pcTaskName);
}

/**
 * @brief Malloc failed hook
 */
void vApplicationMallocFailedHook(void)
{
    panic("Memory allocation failed", NULL);
}

/**
 * @brief Hard Fault Handler override for better debugging
 */
void HardFault_Handler(void)
{
    panic("Hard Fault exception", NULL);
}
