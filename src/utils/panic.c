/**
 * @file panic.c
 * @brief System panic and error handlers for FreeRTOS
 * @author Wiktor Szewczyk
 * @author Patryk Madej
 */

#include "panic.h"

#include "LPC17xx.h"
#include "RTE_Device.h"
#include "config.h"

#include <string.h>

/* Map USART number to LPC17xx UART peripheral */
#if defined(RTE_UART0) && (RTE_UART0 == 1)
#define PANIC_UART      LPC_UART0
#define PANIC_PCONP_BIT (1UL << 3) /* PCUART0 */
#elif defined(RTE_UART1) && (RTE_UART1 == 1)
#define PANIC_UART      LPC_UART1
#define PANIC_PCONP_BIT (1UL << 4) /* PCUART1 */
#elif defined(RTE_UART2) && (RTE_UART2 == 1)
#define PANIC_UART      LPC_UART2
#define PANIC_PCONP_BIT (1UL << 24) /* PCUART2 */
#elif defined(RTE_UART3) && (RTE_UART3 == 1)
#define PANIC_UART      LPC_UART3
#define PANIC_PCONP_BIT (1UL << 25) /* PCUART3 */
#else
#error "No USART enabled in RTE_Device.h. Enable RTE_UART0/1/2/3."
#endif

/* UART Line Status Register bits */
#define LSR_THRE (1 << 5) /* Transmitter Holding Register Empty */

/**
 * @brief Send single character via raw UART (blocking)
 */
static void panic_putc(char c)
{
    while (!(PANIC_UART->LSR & LSR_THRE))
        ;
    PANIC_UART->THR = c;
}

/**
 * @brief Print message using raw UART (blocking)
 * @note Assumes UART already configured by logger
 */
static void panic_print(const char *msg)
{
    while (*msg)
    {
        panic_putc(*msg++);
    }
}

/**
 * @brief Print panic message and halt system
 */
void panic(const char *msg, const char *info)
{
    __disable_irq();
    LPC_SC->PCONP |= PANIC_PCONP_BIT;

    panic_print("\r\n*** PANIC ***\r\n");
    panic_print(msg);
    if (info)
    {
        panic_print(": ");
        panic_print(info);
    }
    panic_print("\r\n");

    while (1)
    {
        __WFI();
    }
}
