/**
 * @file main.c
 * @brief Main application entry point
 * @author Wiktor Szewczyk
 * @author Patryk Madej
 */

#include "LPC17xx.h"
#include "cmsis_os2.h"
#include "config.h"
#include "logger.h"
#include "panic.h"
#include "system.h"
#include "task_acquisition.h"
#include "task_network.h"

/**
 * @brief Main function
 */
int main(void)
{
    osKernelInitialize();

    LOG_INFO("System starting...");
    if (system_init() != 0)
    {
        panic("System initialization failed", NULL);
    }

    LOG_INFO("Starting tasks...");
    if (network_task_start() != 0)
    {
        panic("Failed to start network task", NULL);
    }

    if (acquisition_task_start() != 0)
    {
        panic("Failed to start acquisition task", NULL);
    }

    LOG_INFO("Starting scheduler...");
    osKernelStart();

    panic("Scheduler failed to start", NULL);
    return 0;
}
