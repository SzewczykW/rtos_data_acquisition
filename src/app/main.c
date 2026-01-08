/**
 * @file main.c
 * @brief Main application entry point
 * @author Wiktor Szewczyk
 * @author Patryk Madej
 */

#include "LPC17xx.h"
#include "cmsis_os2.h"
#include "logger.h"
#include "panic.h"
#include "system.h"
#include "task_acquisition.h"
#include "task_network.h"

static void init_task(void *arg)
{
    (void)arg;

    osThreadExit();
}

int main(void)
{
    SystemCoreClockUpdate();

    osStatus_t ret_status;
    ret_status = osKernelInitialize();
    if (ret_status != osOK)
    {
        panic("osKernelInit failed", NULL);
    }

    if (system_init() != 0)
    {
        panic("System initialization failed", NULL);
    }

    LOG_INFO("System started...");

    LOG_INFO("Starting tasks...");
    if (network_task_start() != 0)
    {
        panic("Failed to start network task", NULL);
    }
    LOG_INFO("Network task started");

    if (acquisition_task_start() != 0)
    {
        panic("Failed to start acquisition task", NULL);
    }
    LOG_INFO("Acquisition task started");

    osKernelStart();

    panic("Scheduler failed to start", NULL);
    return 0;
}
