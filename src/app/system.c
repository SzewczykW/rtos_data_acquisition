/**
 * @file system.c
 * @brief CMSIS-RTOS2 RTX5 hooks implementation
 * @author Wiktor Szewczyk
 * @author Patryk Madej
 */

#include "system.h"

#include "LPC17xx.h"
#include "panic.h"
#include "rl_net.h"
#include "rtx_os.h"

#include <stddef.h>

/**
 * @brief Hard Fault Handler override
 */
void HardFault_Handler(void)
{
    panic("Hard Fault exception", NULL);
}

/**
 * @brief osRtxErrorNotify override
 */
uint32_t osRtxErrorNotify(uint32_t code, void *object_id)
{
    (void)object_id;
    switch (code)
    {
        case osRtxErrorStackUnderflow:
            panic("Stack underflow detected", NULL);
        case osRtxErrorISRQueueOverflow:
            panic("ISR Queue overflow detected", NULL);
        case osRtxErrorTimerQueueOverflow:
            panic("Timer Queue overflow detected", NULL);
        case osRtxErrorClibSpace:
            panic("C library heap space exhausted", NULL);
        case osRtxErrorClibMutex:
            panic("C library mutex error", NULL);
        case osRtxErrorSVC:
            panic("SVC call error", NULL);
        default:
            panic("Unknown RTX error", NULL);
    }
}

/**
 * @brief netHandleError override
 */
void netHandleError(netErrorCode error)
{
    switch (error)
    {
        case netErrorMemAlloc:
            panic("NetHandleError: Out of mem error", NULL);
        case netErrorMemFree:
            panic("NetHandleError: Invalid memory free", NULL);
        case netErrorMemCorrupt:
            panic("NetHandleError: Memory corruption detected", NULL);
        case netErrorConfig:
            panic("NetHandleError: Invalid net config", NULL);
        case netErrorRtosCreate:
            panic("NetHandleError: RTOS object creation failed", NULL);
        case netErrorUdpAlloc:
            panic("NetHandleError: Out of UDP Sockets", NULL);
        case netErrorTcpAlloc:
            panic("NetHandleError: Out of TCP Sockets", NULL);
        case netErrorTcpState:
            panic("NetHandleError: TCP State machine in undefined state", NULL);
        default:
            panic("NetHandleError: Unknown error", NULL);
    }
}
