/**
 * @file task_init.c
 * @brief System initialization task
 * @author Wiktor Szewczyk
 * @author Patryk Madej
 */

#include "task_init.h"

#include "LPC17xx.h"
#include "logger.h"
#include "panic.h"
#include "task_acquisition.h"
#include "task_network.h"

static void init_task(void *argument)
{
    (void)argument;

    // Following pin configuration ensures that the PHY RX_ER is always low
    // P1.14 -> GPIO (PINSEL2[29:28]=00)
    LPC_PINCON->PINSEL2 &= ~(3u << 28);

    // Pull-down (PINMODE2[29:28]=10)
    LPC_PINCON->PINMODE2 = (LPC_PINCON->PINMODE2 & ~(3u << 28)) | (2u << 28);

    // GPIO output low
    LPC_GPIO1->FIODIR |= (1u << 14);
    LPC_GPIO1->FIOCLR = (1u << 14);

    // Accept unicast and broadcast but not multicast
    LPC_EMAC->RxFilterCtrl = 0x22;

    if (network_task_start() != 0)
    {
        panic("Failed to start network task", NULL);
    }

    if (acquisition_task_start() != 0)
    {
        panic("Failed to start acquisition task", NULL);
    }

    osThreadExit();
}

int init_task_start(void)
{
    static const osThreadAttr_t init_task_attr = {
        .name       = "init",
        .priority   = TASK_INIT_PRIORITY,
        .stack_size = TASK_INIT_STACK_SIZE,
    };

    return (osThreadNew(init_task, NULL, &init_task_attr) != NULL) ? 0 : -1;
}
