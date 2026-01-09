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
#include "rl_net.h"
#include "task_acquisition.h"
#include "task_init.h"
#include "task_network.h"

int main(void)
{
    SystemCoreClockUpdate();

    osStatus_t st = osKernelInitialize();
    if (st != osOK)
    {
        panic("osKernelInitialize failed", NULL);
    }

    if (init_task_start() != 0)
    {
        panic("init_task_start failed", NULL);
    }

    if (logger_init() != LOGGER_OK)
    {
        panic("Logger init failed", NULL);
    }

    if (netInitialize() != netOK)
    {
        panic("Network stack initialization failed", NULL);
    }

    if (network_init() != 0)
    {
        panic("Network init failed", NULL);
    }

    if (acquisition_init() != 0)
    {
        panic("Acquisition init failed", NULL);
    }

    st = osKernelStart();
    if (st != osOK)
    {
        panic("osKernelStart failed", NULL);
    }

    while (1)
    {
        __WFI();
    }
}
